#include "BrainfuckJITCompiler.hh"

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

#include "Common.hh"

using namespace std;


static inline bool is_bf_command(char cmd) {
  return (cmd == '+') || (cmd == '-') || (cmd == '<') || (cmd == '>') ||
         (cmd == '[') || (cmd == ']') || (cmd == ',') || (cmd == '.');
}


BrainfuckJITCompiler::BrainfuckJITCompiler(const string& filename,
    size_t mem_size, size_t cell_size, int optimize_level,
    size_t expansion_size, uint64_t debug_flags) :
    expansion_size(expansion_size), cell_size(cell_size),
    optimize_level(optimize_level), debug_flags(debug_flags) {
  this->code = load_file(filename);

  try {
    this->operand_size = this->cell_size_to_operand_size.at(this->cell_size);
  } catch (const out_of_range&) {
    throw invalid_argument("cell size must be 1, 2, 4, or 8");
  }

  // strip all the non-opcode data out of the code
  char* write_ptr = const_cast<char*>(this->code.data());
  for (char ch : this->code) {
    if (is_bf_command(ch)) {
      *(write_ptr++) = ch;
    }
  }
  this->code.resize(write_ptr - this->code.data());
}


pair<map<ssize_t, ssize_t>, size_t> BrainfuckJITCompiler::get_mover_loop_info(
    size_t offset) {
  // a loop is a mover loop if all of the following are true:
  // 1. the loop only contains <>-+
  // 2. the loop contains the same number of < and >
  // 3. the loop decrements the starting cell by 1 every time
  // these loops can be optimized into various add/sub opcodes without actually
  // moving rbx, which saves quite a bit of time since these loops are pretty
  // common in brainfuck programs

  if (this->code[offset] != '[') {
    throw logic_error("get_mover_loop_info called on non-loop");
  }

  map<ssize_t, ssize_t> ret;
  size_t start_offset = offset;
  ssize_t offset_delta = 0;
  for (offset++; offset < this->code.size(); offset++) {
    if (this->code[offset] == '<') {
      offset_delta--;
    } else if (this->code[offset] == '>') {
      offset_delta++;
    } else if (this->code[offset] == '+') {
      ret[offset_delta]++;
    } else if (this->code[offset] == '-') {
      ret[offset_delta]--;
    } else if (this->code[offset] == ']') {
      break;
    } else {
      return make_pair(map<ssize_t, ssize_t>(), 0); // not a mover loop
    }
  }
  // if the loop didn't end or didn't leave the pointer in the same place as
  // when it started, it's not a mover loop
  if ((offset >= this->code.size()) || (offset_delta != 0)) {
    return make_pair(map<ssize_t, ssize_t>(), 0);
  }

  return make_pair(ret, offset + 1 - start_offset);
}


void BrainfuckJITCompiler::write_load_cell_value(AMD64Assembler& as,
    const MemoryReference& dest, const MemoryReference& src) {
  if (this->cell_size == 1) {
    as.write_movzx8(dest, src);
  } else if (this->cell_size == 2) {
    as.write_movzx16(dest, src);
  } else if (this->cell_size == 4) {
    as.write_xor(dest, dest);
    as.write_mov(dest, src, OperandSize::DoubleWord);
  } else {
    as.write_mov(dest, src);
  }
}


void BrainfuckJITCompiler::execute() {
  AMD64Assembler as;

  // r12 = memory ptr
  // r13 = end ptr (address of last valid byte)
  // rbx = current ptr
  // r14 = putchar
  // r15 = getchar

  // generate lead-in code
  as.write_push(rbp);
  as.write_mov(rbp, rsp);
  as.write_push(rbx);
  as.write_push(r12);
  as.write_push(r13);
  as.write_push(r14);
  as.write_push(r15);

  // allocate memory block
  as.write_mov(rdi, expansion_size);
  as.write_mov(rsi, cell_size);
  as.write_mov(rax, reinterpret_cast<int64_t>(&calloc));
  as.write_sub(rsp, 8);
  as.write_call(rax);
  as.write_add(rsp, 8);
  as.write_mov(r12, rax);
  as.write_lea(r13, MemoryReference(rax, expansion_size - cell_size));
  as.write_mov(rbx, rax);
  as.write_mov(r14, reinterpret_cast<int64_t>(&putchar));
  as.write_mov(r15, reinterpret_cast<int64_t>(&getchar));

  // generate assembly
  vector<size_t> jump_offsets;
  size_t count = 0;
  for (size_t offset = 0; offset < this->code.size(); offset += count) {
    char opcode = this->code[offset];
    count = 1;
    if (optimize_level) {
      for (; (offset + count < this->code.size()) && (this->code[offset + count] == opcode); count++);
    }

    switch (opcode) {
      case '+':
        if (count == 1) {
          as.write_inc(MemoryReference(rbx, 0), this->operand_size);
        } else {
          as.write_add(MemoryReference(rbx, 0), count, this->operand_size);
        }
        break;

      case '-':
        if (count == 1) {
          as.write_dec(MemoryReference(rbx, 0), this->operand_size);
        } else {
          as.write_sub(MemoryReference(rbx, 0), count, this->operand_size);
        }
        break;

      case '>':
        if (count == 1 && this->cell_size == 1) {
          as.write_inc(rbx);
        } else {
          as.write_add(rbx, count * this->cell_size);
        }

        // expand the memory space if needed
        as.write_cmp(rbx, r13);
        as.write_jle(string_printf("%zu_MoveRight_skip_expand", offset));
        as.write_call("expand");
        as.write_label(string_printf("%zu_MoveRight_skip_expand", offset));
        break;

      case '<':
        if (count == 1 && this->cell_size == 1) {
          as.write_dec(rbx);
        } else {
          as.write_sub(rbx, count * this->cell_size);
        }

        // note: using a conditional move is slower here, probably because the
        // case where the move actually occurs is rare
        as.write_cmp(rbx, r12);
        as.write_jge(string_printf("%zu_MoveLeft_skip", offset));
        as.write_mov(rbx, r12);
        as.write_label(string_printf("%zu_MoveLeft_skip", offset));
        break;

      case '[':
        if (this->optimize_level > 1) {
          // optimization: turn mover loops into better opcodes
          auto mi = this->get_mover_loop_info(offset);
          // if (0, -1) exists, then this is a mover loop. mi.first[0] may
          // create mi.first[0], but then it will have the value 0, so we won't
          // incorrectly think it's a mover loop when it isn't
          if (mi.first[0] == -1) {
            count = mi.second;

            // if there's only one entry, it must be the (0, -1) entry. just
            // clear the current cell
            if (mi.first.size() == 1) {
              as.write_label(string_printf("%zu_OptimizedZeroCell", offset));
              as.write_mov(MemoryReference(rbx, 0), 0, this->operand_size);
              break;
            }

            as.write_label(string_printf("%zu_OptimizedMoverLoop", offset));

            //ssize_t min_offset = mi.first.begin()->first;
            ssize_t max_offset = mi.first.rbegin()->first;

            // TODO: we should do left-bound checking here

            // expand the memory space if the loop will hit the right bound
            if (max_offset > 0) {
              // most of the time, we won't need to expand, so use a scratch
              // register to avoid having to fix rbx when we don't expand
              as.write_lea(rax, MemoryReference(rbx, max_offset * this->cell_size));
              as.write_cmp(rax, r13);
              as.write_jle(string_printf("%zu_OptimizedMoverLoop_skip_expand", offset));
              as.write_mov(rbx, rax);
              as.write_call("expand");
              as.write_sub(rbx, max_offset * this->cell_size);
              as.write_label(string_printf("%zu_OptimizedMoverLoop_skip_expand", offset));
            }

            // read the value
            this->write_load_cell_value(as, rax, MemoryReference(rbx, 0));

            // update the appropriate cells
            // TODO: we can optimize this further by grouping cells with the
            // same multiplier together, so we don't have to recompute rcx
            for (const auto& it : mi.first) {
              ssize_t offset = it.first;
              ssize_t mult = it.second;

              if (mult == 0) {
                continue;
              } else if (mult == 1) {
                as.write_add(MemoryReference(rbx, offset * this->cell_size),
                    rax, this->operand_size);
              } else if (mult == -1) {
                if (offset == 0) {
                  as.write_mov(MemoryReference(rbx, 0), 0, this->operand_size);
                } else {
                  as.write_sub(MemoryReference(rbx, offset * this->cell_size),
                      rax, this->operand_size);
                }
              } else {
                as.write_imul_imm(rcx, rax, mult);
                as.write_add(MemoryReference(rbx, offset * this->cell_size),
                    rcx, this->operand_size);
              }
            }

            break;
          }
        }

        for (size_t x = 0; x < count; x++) {
          jump_offsets.emplace_back(offset + x);
          as.write_cmp(MemoryReference(rbx, 0), 0, this->operand_size);
          as.write_je(string_printf("jump_%zu_end", jump_offsets.back()));
          as.write_label(string_printf("jump_%zu_begin", jump_offsets.back()));
        }
        break;

      case ']':
        for (size_t x = 0; x < count; x++) {
          if (jump_offsets.empty()) {
            throw runtime_error("unbalanced braces");
          }
          as.write_cmp(MemoryReference(rbx, 0), 0, this->operand_size);
          as.write_jne(string_printf("jump_%zu_begin", jump_offsets.back()));
          as.write_label(string_printf("jump_%zu_end", jump_offsets.back()));
          jump_offsets.pop_back();
        }
        break;

      case '.':
        as.write_sub(rsp, 8);
        for (size_t x = 0; x < count; x++) {
          this->write_load_cell_value(as, rdi, MemoryReference(rbx, 0));
          as.write_call(r14);
        }
        as.write_add(rsp, 8);
        break;

      case ',':
        as.write_sub(rsp, 8);
        for (size_t x = 0; x < count; x++) {
          as.write_call(r15);
          as.write_mov(MemoryReference(rbx, 0), rax, this->operand_size);
        }
        as.write_add(rsp, 8);
        break;
    }
  }

  // generate lead-out code
  as.write_pop(r15);
  as.write_pop(r14);
  as.write_pop(r13);
  as.write_pop(r12);
  as.write_pop(rbx);
  as.write_pop(rbp);
  as.write_ret();

  {
    // write the expand subroutine. this breaks the system v convention
    as.write_label("expand");

    // convert rbx and r13 from pointers to offset and size
    as.write_sub(rbx, r12);
    as.write_sub(r13, r12);
    as.write_add(r13, this->cell_size);

    // expand the data block so it ends at the next 64K boundary after the new
    // current offset

    // pass the existing data block pointer as the first argument
    as.write_mov(rdi, r12);

    // pass the new size as the second argument - this is the current offset
    // rounded up to the next 64KB boundary.
    as.write_mov(rsi, rbx);
    as.write_add(rsi, (this->expansion_size * this->cell_size));
    as.write_and(rsi, ~((this->expansion_size * this->cell_size) - 1));

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
    as.write_lea(r13, MemoryReference(r12, -this->cell_size, r13));
    as.write_add(rbx, r12);
    as.write_ret();
  }

  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  string data = as.assemble(&patch_offsets, &compiled_labels);
  void* executable_data = this->buf.append(data, &patch_offsets);
  void (*function)() = reinterpret_cast<void(*)()>(executable_data);

  if (this->debug_flags & DebugFlag::ShowAssembly) {
    string disassembly = AMD64Assembler::disassemble(executable_data,
        data.size(), reinterpret_cast<int64_t>(executable_data),
        &compiled_labels);
    fprintf(stderr, "%s\n", disassembly.c_str());
    string size_str = format_size(data.size());
    fprintf(stderr, "code buffer size: %s\n", size_str.c_str());
  }

  function();
}


const unordered_map<size_t, OperandSize> BrainfuckJITCompiler::cell_size_to_operand_size({
  {1, OperandSize::Byte},
  {2, OperandSize::Word},
  {4, OperandSize::DoubleWord},
  {8, OperandSize::QuadWord},
});
