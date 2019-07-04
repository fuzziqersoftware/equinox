#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <deque>
#include <memory>
#include <phosg/Filesystem.hh>
#include <phosg/Process.hh>
#include <phosg/Strings.hh>
#include <phosg/Time.hh>
#include <string>

#include <libamd64/AMD64Assembler.hh>
#include <libamd64/CodeBuffer.hh>

using namespace std;


static inline bool is_bf_command(char cmd) {
  return (cmd == '+') || (cmd == '-') || (cmd == '<') || (cmd == '>') ||
         (cmd == '[') || (cmd == ']') || (cmd == ',') || (cmd == '.');
}


static pair<map<ssize_t, ssize_t>, size_t> get_mover_loop_info(const string& code, size_t offset) {
  // a loop is a mover loop if all of the following are true:
  // 1. the loop only contains <>-+
  // 2. the loop contains the same number of < and >
  // 3. the loop decrements the starting cell by 1 every time
  // these loops can be optimized into various add/sub opcodes without actually
  // moving rbx, which saves quite a bit of time since these loops are pretty
  // common in brainfuck programs

  if (code[offset] != '[') {
    throw logic_error("get_mover_loop_info called on non-loop");
  }

  map<ssize_t, ssize_t> ret;
  size_t start_offset = offset;
  ssize_t offset_delta = 0;
  for (offset++; offset < code.size(); offset++) {
    if (code[offset] == '<') {
      offset_delta--;
    } else if (code[offset] == '>') {
      offset_delta++;
    } else if (code[offset] == '+') {
      ret[offset_delta]++;
    } else if (code[offset] == '-') {
      ret[offset_delta]--;
    } else if (code[offset] == ']') {
      break;
    } else {
      return make_pair(map<ssize_t, ssize_t>(), 0); // not a mover loop
    }
  }
  // if the loop didn't end or didn't leave the pointer in the same place as
  // when it started, it's not a mover loop
  if ((offset >= code.size()) || (offset_delta != 0)) {
    return make_pair(map<ssize_t, ssize_t>(), 0);
  }

  return make_pair(ret, offset + 1 - start_offset);
}


void bf_execute(const char* filename, size_t mem_size, int optimize_level,
    size_t expansion_size, bool disassemble) {
  string code = load_file(filename);

  // strip all the non-opcode data out of it
  {
    char* write_ptr = const_cast<char*>(code.data());
    for (char ch : code) {
      if (is_bf_command(ch)) {
        *(write_ptr++) = ch;
      }
    }
    code.resize(write_ptr - code.data());
  }

  AMD64Assembler as;

  // r12 = memory ptr
  // r13 = end ptr (address of last valid byte)
  // rbx = current ptr
  // r14 = putchar
  // r15 = getchar

  // generate lead-in code
  as.write_push(Register::RBP);
  as.write_mov(rbp, rsp);
  as.write_push(Register::RBX);
  as.write_push(Register::R12);
  as.write_push(Register::R13);
  as.write_push(Register::R14);
  as.write_push(Register::R15);

  // allocate memory block
  as.write_mov(rdi, expansion_size);
  as.write_mov(rsi, 1);
  as.write_mov(rax, reinterpret_cast<int64_t>(&calloc));
  as.write_sub(rsp, 8);
  as.write_call(rax);
  as.write_add(rsp, 8);
  as.write_mov(r12, rax);
  as.write_lea(r13, MemoryReference(rax, expansion_size - 1));
  as.write_mov(rbx, rax);
  as.write_mov(r14, reinterpret_cast<int64_t>(&putchar));
  as.write_mov(r15, reinterpret_cast<int64_t>(&getchar));

  // generate assembly
  vector<size_t> jump_offsets;
  size_t count = 0;
  for (size_t offset = 0; offset < code.size(); offset += count) {
    int opcode = (offset == code.size()) ? EOF : code[offset];
    count = 1;
    if (optimize_level) {
      for (; (offset + count < code.size()) && (code[offset + count] == opcode); count++);
    }

    switch (opcode) {
      case '+':
        if (count == 1) {
          as.write_inc(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_add(MemoryReference(Register::RBX, 0), count, OperandSize::Byte);
        }
        break;

      case '-':
        if (count == 1) {
          as.write_dec(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_sub(MemoryReference(Register::RBX, 0), count, OperandSize::Byte);
        }
        break;

      case '>':
        if (count == 1) {
          as.write_inc(rbx);
        } else {
          as.write_add(rbx, count);
        }

        // expand the memory space if needed
        as.write_cmp(rbx, r13);
        as.write_jle(string_printf("%zu_MoveRight_skip_expand", offset));
        as.write_call("expand");
        as.write_label(string_printf("%zu_MoveRight_skip_expand", offset));
        break;

      case '<':
        if (count == 1) {
          as.write_dec(rbx);
        } else {
          as.write_sub(rbx, count);
        }

        // note: using a conditional move is slower here, probably because the
        // case where the move actually occurs is rare
        as.write_cmp(rbx, r12);
        as.write_jge(string_printf("%zu_MoveLeft_skip", offset));
        as.write_mov(rbx, r12);
        as.write_label(string_printf("%zu_MoveLeft_skip", offset));
        break;

      case '[':
        if (optimize_level > 1) {
          // optimization: turn mover loops into better opcodes
          auto mi = get_mover_loop_info(code, offset);
          // if (0, -1) exists, then this is a mover loop. mi.first[0] may
          // create mi.first[0], but then it will have the value 0, so we won't
          // incorrectly think it's a mover loop when it isn't
          if (mi.first[0] == -1) {
            count = mi.second;

            // if there's only one entry, it must be the (0, -1) entry. just
            // clear the current cell
            if (mi.first.size() == 1) {
              as.write_label(string_printf("%zu_OptimizedZeroCell", offset));
              as.write_mov(MemoryReference(Register::RBX, 0), 0, OperandSize::Byte);
              break;
            }

            as.write_label(string_printf("%zu_OptimizedMoverLoop", offset));

            //ssize_t min_offset = mi.first.begin()->first;
            ssize_t max_offset = mi.first.rbegin()->first;

            // TODO: we should do left-bound checking here

            // expand the memory space if the loop will hit the right bound
            // TODO: for ridiculous loops, we might need to expand multiple
            // times; implement this case
            if (max_offset > 0) {
              // most of the time, we won't need to expand, so use a scratch
              // register to avoid having to fix rbx when we don't expand
              as.write_lea(rax, MemoryReference(rbx, max_offset));
              as.write_cmp(rax, r13);
              as.write_jle(string_printf("%zu_OptimizedMoverLoop_skip_expand", offset));
              as.write_mov(rbx, rax);
              as.write_call("expand");
              as.write_sub(rbx, max_offset);
              as.write_label(string_printf("%zu_OptimizedMoverLoop_skip_expand", offset));
            }

            // read the value and write it to the appropriate cells
            // TODO: we can optimize this further by grouping cells with the
            // same multiplier together, so we don't have to recompute rcx
            as.write_movzx8(rax, MemoryReference(Register::RBX, 0));
            for (const auto& it : mi.first) {
              ssize_t offset = it.first;
              ssize_t mult = it.second;

              if (mult == 0) {
                continue;
              } else if (mult == 1) {
                as.write_add(MemoryReference(Register::RBX, offset), al, OperandSize::Byte);
              } else if (mult == -1) {
                if (offset == 0) {
                  as.write_mov(MemoryReference(Register::RBX, 0), 0, OperandSize::Byte);
                } else {
                  as.write_sub(MemoryReference(Register::RBX, offset), al, OperandSize::Byte);
                }
              } else {
                as.write_imul_imm(rcx, rax, mult);
                as.write_add(MemoryReference(rbx, offset), rcx, OperandSize::Byte);
              }
            }

            break;
          }
        }

        for (size_t x = 0; x < count; x++) {
          jump_offsets.emplace_back(offset + x);
          as.write_cmp(MemoryReference(rbx, 0), 0, OperandSize::Byte);
          as.write_je(string_printf("jump_%zu_end", jump_offsets.back()));
          as.write_label(string_printf("jump_%zu_begin", jump_offsets.back()));
        }
        break;

      case ']':
        for (size_t x = 0; x < count; x++) {
          if (jump_offsets.empty()) {
            throw runtime_error("unbalanced braces");
          }
          as.write_cmp(MemoryReference(Register::RBX, 0), 0, OperandSize::Byte);
          as.write_jne(string_printf("jump_%zu_begin", jump_offsets.back()));
          as.write_label(string_printf("jump_%zu_end", jump_offsets.back()));
          jump_offsets.pop_back();
        }
        break;

      case '.':
        as.write_sub(rsp, 8);
        for (size_t x = 0; x < count; x++) {
          as.write_movzx8(Register::RDI, MemoryReference(Register::RBX, 0));
          as.write_call(r14);
        }
        as.write_add(rsp, 8);
        break;

      case ',':
        as.write_sub(rsp, 8);
        for (size_t x = 0; x < count; x++) {
          as.write_call(r15);
          as.write_mov(MemoryReference(Register::RBX, 0), al, OperandSize::Byte);
        }
        as.write_add(rsp, 8);
        break;
    }
  }

  // generate lead-out code
  as.write_pop(Register::R15);
  as.write_pop(Register::R14);
  as.write_pop(Register::R13);
  as.write_pop(Register::R12);
  as.write_pop(Register::RBX);
  as.write_pop(Register::RBP);
  as.write_ret();

  {
    // write the expand subroutine. this breaks the system v convention
    as.write_label("expand");

    // convert rbx and r13 from pointers to offset and size
    as.write_sub(rbx, r12);
    as.write_sub(r13, r12);
    as.write_inc(r13);

    // expand the data block so it ends at the next 64K boundary after the new
    // current offset

    // pass the existing data block pointer as the first argument
    as.write_mov(rdi, r12);

    // pass the new size as the second argument - this is the current offset
    // rounded up to the next 64KB boundary.
    as.write_mov(rsi, rbx);
    as.write_add(rsi, expansion_size);
    as.write_and(rsi, ~(expansion_size - 1));

    // store the old size in r12, and the new size in r13
    as.write_mov(r12, r13);
    as.write_mov(r13, rsi);

    // call realloc
    as.write_mov(rax, reinterpret_cast<int64_t>(&realloc));
    as.write_call(rax);

    // call memset to clear the new bytes. also save the block ptr to r12
    as.write_lea(rdi, MemoryReference(rax, 0, r12)); // data + old_size
    as.write_xor(rsi, rsi); // 0
    as.write_mov(rdx, r13); // new_size - old_size
    as.write_sub(rdx, r12);
    as.write_mov(r12, rax); // store data pointer in r12
    as.write_mov(rax, reinterpret_cast<int64_t>(&memset));
    as.write_call(rax);

    // convert r13 and rbx back to pointers, and we're done
    as.write_lea(r13, MemoryReference(r12, -1, r13));
    as.write_add(rbx, r12);
    as.write_ret();
  }

  CodeBuffer buf;

  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  string data = as.assemble(&patch_offsets, &compiled_labels);
  void* executable_data = buf.append(data, &patch_offsets);
  void (*function)() = reinterpret_cast<void(*)()>(executable_data);

  if (disassemble) {
    string disassembly = AMD64Assembler::disassemble(executable_data,
        data.size(), reinterpret_cast<int64_t>(executable_data),
        &compiled_labels);
    fprintf(stderr, "%s\n", disassembly.c_str());
  }

  function();
}
