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


static bool is_bf_command(char cmd) {
  return (cmd == '+') || (cmd == '-') || (cmd == '<') || (cmd == '>') ||
         (cmd == '[') || (cmd == ']') || (cmd == ',') || (cmd == '.');
}


void bf_interpret(const char* filename, size_t expansion_size) {
  string code = load_file(filename);

  deque<string> memory_blocks;
  memory_blocks.emplace_back(expansion_size, 0);
  deque<string>::iterator current_block = memory_blocks.begin();
  size_t block_offset = 0;
  size_t pc = 0;

  while (pc < code.size()) {
    switch (code[pc]) {
      case '>':
        if (block_offset < current_block->size() - 1) {
          block_offset++;

        } else {
          if (current_block == memory_blocks.end() - 1) {
            memory_blocks.emplace_back(expansion_size, 0);
          }
          current_block++;
          block_offset = 0;
        }
        break;

      case '<':
        if (block_offset) {
          block_offset--;
        } else {
          if (current_block != memory_blocks.begin()) {
            current_block--;
          }
          block_offset = current_block->size() - 1;
        }
        break;

      case '+':
        (*current_block)[block_offset]++;
        break;

      case '-':
        (*current_block)[block_offset]--;
        break;

      case '.':
        putchar((*current_block)[block_offset]);
        break;

      case ',':
        (*current_block)[block_offset] = getchar();
        break;

      case '[':
        if ((*current_block)[block_offset] == 0) {
          size_t brace_level = 0;
          for (pc++; pc < code.size(); pc++) {
            if (code[pc] == '[') {
              brace_level++;
            } else if (code[pc] == ']') {
              if (brace_level == 0) {
                break;
              } else {
                brace_level--;
              }
            }
          }
          if (brace_level) {
            throw runtime_error("unbalanced braces");
          }
        }
        break;

      case ']':
        if ((*current_block)[block_offset] != 0) {
          if (!pc) {
            throw runtime_error("close brace in first position");
          }

          size_t brace_level = 0;
          for (pc--; pc > 0; pc--) {
            if (code[pc] == ']') {
              brace_level++;
            } else if (code[pc] == '[') {
              if (brace_level == 0) {
                break;
              } else {
                brace_level--;
              }
            }
          }
          if (brace_level) {
            throw runtime_error("unbalanced braces");
          }
        }
        break;
    }
    pc++;
  }
}


void bf_execute(const char* filename, size_t mem_size, int optimize_level,
    size_t expansion_size, bool disassemble) {
  string code = load_file(filename);

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
  char prev_opcode = 1;
  size_t num_same_opcode = 1;
  vector<size_t> jump_offsets;
  for (size_t offset = 0; offset <= code.size(); offset++) {
    int opcode = (offset == code.size()) ? EOF : code[offset];
    if ((opcode != EOF) && !is_bf_command(opcode)) {
      continue;
    }
    if (optimize_level) {
      if (opcode == prev_opcode) {
        num_same_opcode++;
        continue;
      }
    }

    switch (prev_opcode) {
      case '+':
        if (num_same_opcode == 1) {
          as.write_inc(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_add(MemoryReference(Register::RBX, 0), num_same_opcode, OperandSize::Byte);
        }
        break;

      case '-':
        if (num_same_opcode == 1) {
          as.write_dec(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_sub(MemoryReference(Register::RBX, 0), num_same_opcode, OperandSize::Byte);
        }
        break;

      case '>':
        if (num_same_opcode == 1) {
          as.write_inc(rbx);
        } else {
          as.write_add(rbx, num_same_opcode);
        }

        // expand the memory space if needed
        as.write_cmp(rbx, r13);
        as.write_jle(string_printf("%zu_MoveRight_skip_expand", offset));
        as.write_call("expand");
        as.write_label(string_printf("%zu_MoveRight_skip_expand", offset));
        break;

      case '<':
        as.write_cmp(rbx, r12);
        as.write_jle(string_printf("%zu_MoveLeft_skip", offset));
        if (num_same_opcode == 1) {
          as.write_dec(rbx);
        } else {
          as.write_sub(rbx, num_same_opcode);
        }
        as.write_label(string_printf("%zu_MoveLeft_skip", offset));
        break;

      case '[':
        for (; num_same_opcode > 0; num_same_opcode--) {
          jump_offsets.emplace_back(offset - num_same_opcode);
          as.write_cmp(MemoryReference(Register::RBX, 0), 0, OperandSize::Byte);
          as.write_je(string_printf("jump_%zu_end", offset - num_same_opcode));
          as.write_label(string_printf("jump_%zu_begin", offset - num_same_opcode));
        }
        break;

      case ']':
        for (; num_same_opcode > 0; num_same_opcode--) {
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
        for (; num_same_opcode > 0; num_same_opcode--) {
          as.write_movzx8(Register::RDI, MemoryReference(Register::RBX, 0));
          as.write_call(r14);
        }
        as.write_add(rsp, 8);
        break;

      case ',':
        as.write_sub(rsp, 8);
        for (; num_same_opcode > 0; num_same_opcode--) {
          as.write_call(r15);
          as.write_mov(MemoryReference(Register::RBX, 0), al, OperandSize::Byte);
        }
        as.write_add(rsp, 8);
        break;
    }
    prev_opcode = opcode;
    num_same_opcode = 1;
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


void bf_compile(const char* input_filename, const char* output_filename,
    size_t mem_size, int optimize_level, bool skip_assembly, bool intel_syntax) {

  shared_ptr<FILE> source;
  if (!strcmp(input_filename, "-")) {
    source.reset(stdin, [](FILE*) { });
  } else {
    source = fopen_shared(input_filename, "rt");
  }

  shared_ptr<FILE> out;
  unique_ptr<Subprocess> assembler_process;
  if (skip_assembly) {
    out = fopen_shared(output_filename, "wt");

  } else {
    vector<string> cmd({"gcc", "-o", output_filename, "-m64", "-x",
        "assembler-with-cpp", "-"});
    assembler_process.reset(new Subprocess(cmd, -1, 1, 2));
    out = fdopen_shared(assembler_process->stdin(), "wt");
  }

  if (intel_syntax) {
    fprintf(out.get(), ".intel_syntax noprefix\n\n");
  }

  // make the jump buffer
  vector<int> jump_ids;
  int latest_jump_id = 0;

  // generate lead-in code (allocates the array) and zero the memory block
  fprintf(out.get(), ".globl _main\n");
  fprintf(out.get(), "_main:\n");
  if (intel_syntax) {
    fprintf(out.get(), "  push    rbp\n");
    fprintf(out.get(), "  mov     rbp, rsp\n");
    fprintf(out.get(), "  mov     rdi, %zu\n", mem_size);
    fprintf(out.get(), "  call    _malloc\n");
    fprintf(out.get(), "  mov     r14, rax\n");
    fprintf(out.get(), "  mov     r12, r14\n");
    fprintf(out.get(), "  lea     r13, [r14 + %zu]\n", (mem_size - 1));
    fprintf(out.get(), "0:\n");
    fprintf(out.get(), "  mov     byte ptr [r12], 0\n");
    fprintf(out.get(), "  inc     r12\n");
    fprintf(out.get(), "  cmp     r12, r13\n");
    fprintf(out.get(), "  jle     0b\n");
    fprintf(out.get(), "  mov     r12, r14\n");
  } else {
    fprintf(out.get(), "  push    %%rbp\n");
    fprintf(out.get(), "  mov     %%rsp, %%rbp\n");
    fprintf(out.get(), "  mov     $%zu, %%rdi\n", mem_size);
    fprintf(out.get(), "  call    _malloc\n");
    fprintf(out.get(), "  mov     %%rax, %%r14\n");
    fprintf(out.get(), "  mov     %%r14, %%r12\n");
    fprintf(out.get(), "  lea     %zu(%%r14), %%r13\n", (mem_size - 1));
    fprintf(out.get(), "0:\n");
    fprintf(out.get(), "  movb    $0, (%%r12)\n");
    fprintf(out.get(), "  inc     %%r12\n");
    fprintf(out.get(), "  cmp     %%r13, %%r12\n");
    fprintf(out.get(), "  jle     0b\n");
    fprintf(out.get(), "  mov     %%r14, %%r12\n");
  }

  // r12 = current ptr
  // r13 = end ptr
  // r14 = begin ptr

  // generate assembly
  char prev = 1;
  int num_prev = 1;
  while (prev != EOF) {
    int srcval = fgetc(source.get());
    if (srcval != EOF && !is_bf_command(srcval)) {
      continue;
    }
    if (optimize_level) {
      if (srcval == prev) {
        num_prev++;
        continue;
      }
    }
    switch (prev) {
      case '+':
        fprintf(out.get(), "%ld_Increment:\n", ftell(source.get()));
        if (num_prev == 1) {
          fprintf(out.get(), intel_syntax ? "  inc     byte ptr [r12]\n" :
              "  incb    (%%r12)\n");
        } else {
          fprintf(out.get(), intel_syntax ? "  add     byte ptr [r12], %d\n" :
              "  addb    $%d, (%%r12)\n", num_prev);
        }
        break;

      case '-':
        fprintf(out.get(), "%ld_Decrement:\n", ftell(source.get()));
        if (num_prev == 1) {
          fprintf(out.get(), intel_syntax ? "  dec     byte ptr [r12]\n" :
              "  decb    (%%r12)\n");
        } else {
          fprintf(out.get(), intel_syntax ? "  sub     byte ptr [r12], %d\n" :
              "  subb    $%d, (%%r12)\n", num_prev);
        }
        break;

      case '>':
        fprintf(out.get(), "%ld_MoveRight:\n", ftell(source.get()));
        if (num_prev == 1) {
          if (optimize_level < 2) {
            fprintf(out.get(), intel_syntax ? "  cmp     r12, r13\n" :
                "  cmp     %%r13, %%r12\n");
            fprintf(out.get(), "  jge     1f\n");
          }
          fprintf(out.get(), intel_syntax ? "  inc     r12\n" : "  inc     %%r12\n");
        } else {
          if (optimize_level < 2) {
            if (intel_syntax) {
              fprintf(out.get(), "  mov     rax, r12\n");
              fprintf(out.get(), "  add     rax, %d\n", num_prev);
              fprintf(out.get(), "  cmp     rax, r13\n");
              fprintf(out.get(), "  jge     0f\n");
              fprintf(out.get(), "  mov     r12, rax\n");
              fprintf(out.get(), "  jmp     1f\n");
              fprintf(out.get(), "0:\n");
              fprintf(out.get(), "  mov     r12, r13\n");
            } else {
              fprintf(out.get(), "  mov %%r12, %%rax\n");
              fprintf(out.get(), "  add $%d, %%rax\n", num_prev);
              fprintf(out.get(), "  cmp %%r13, %%rax\n");
              fprintf(out.get(), "  jge 0f\n");
              fprintf(out.get(), "  mov %%rax, %%r12\n");
              fprintf(out.get(), "  jmp 1f\n");
              fprintf(out.get(), "0:\n");
              fprintf(out.get(), "  mov %%r13, %%r12\n");
            }
          } else {
            fprintf(out.get(), intel_syntax ? "  add     r12, %d\n" :
                "  add     $%d, %%r12\n", num_prev);
          }
        }
        fprintf(out.get(), "1:\n");
        break;

      case '<':
        fprintf(out.get(), "%ld_MoveLeft:\n", ftell(source.get()));
        if (num_prev == 1) {
          if (optimize_level < 2) {
            fprintf(out.get(), intel_syntax ? "  cmp     r12, r14\n" :
                "  cmp     %%r14, %%r12\n");
            fprintf(out.get(), "  jle     1f\n");
          }
          fprintf(out.get(), intel_syntax ? "  dec     r12\n" :
              "  dec     %%r12\n");
        } else {
          if (optimize_level < 2) {
            if (intel_syntax) {
              fprintf(out.get(), "  mov     rax, r12\n");
              fprintf(out.get(), "  sub     rax, %d\n", num_prev);
              fprintf(out.get(), "  cmp     rax, r14\n");
              fprintf(out.get(), "  jle     0f\n");
              fprintf(out.get(), "  mov     r12, rax\n");
              fprintf(out.get(), "  jmp     1f\n");
              fprintf(out.get(), "0:\n");
              fprintf(out.get(), "  mov     r12, r14\n");
            } else {
              fprintf(out.get(), "  mov     %%r12, %%rax\n");
              fprintf(out.get(), "  sub     $%d, %%rax\n", num_prev);
              fprintf(out.get(), "  cmp     %%r14, %%rax\n");
              fprintf(out.get(), "  jle     0f\n");
              fprintf(out.get(), "  mov     %%rax, %%r12\n");
              fprintf(out.get(), "  jmp     1f\n");
              fprintf(out.get(), "0:\n");
              fprintf(out.get(), "  mov     %%r14, %%r12\n");
            }
          } else {
            fprintf(out.get(), intel_syntax ? "sub     r12, %d\n" :
                "  sub     $%d, %%r12\n", num_prev);
          }
        }
        fprintf(out.get(), "1:\n");
        break;

      case '[':
        fprintf(out.get(), "%ld_OpenBrace:\n", ftell(source.get()));
        for (; num_prev > 0; num_prev--) {
          jump_ids.emplace_back(latest_jump_id++);
          fprintf(out.get(), intel_syntax ? "  cmp     byte ptr [r12], 0\n" :
              "  cmpb    $0, (%%r12)\n");
          fprintf(out.get(), "  je      jump_%d_end\n", jump_ids.back());
          fprintf(out.get(), "jump_%d_begin:\n", jump_ids.back());
        }
        break;

      case ']':
        fprintf(out.get(), "%ld_CloseBrace:\n", ftell(source.get()));
        for (; num_prev > 0; num_prev--) {
          if (jump_ids.empty()) {
            throw runtime_error("unbalanced braces");
          }
          fprintf(out.get(), intel_syntax ? "  cmp     byte ptr [r12], 0\n" :
              "  cmpb $0, (%%r12)\n");
          fprintf(out.get(), "  jne     jump_%d_begin\n", jump_ids.back());
          fprintf(out.get(), "jump_%d_end:\n", jump_ids.back());
          jump_ids.pop_back();
        }
        break;

      case '.':
        fprintf(out.get(), "%ld_Input:\n", ftell(source.get()));
        for (; num_prev > 0; num_prev--) {
          fprintf(out.get(), intel_syntax ? "  movzx   rdi, byte ptr [r12]\n" :
              "  movzbq  (%%r12), %%rdi\n");
          fprintf(out.get(), "  call    _putchar\n");
        }
        break;

      case ',':
        fprintf(out.get(), "%ld_Output:\n", ftell(source.get()));
        for (; num_prev > 0; num_prev--) {
          fprintf(out.get(), "  call _getchar\n");
          fprintf(out.get(), intel_syntax ? "mov     [r12], al\n" :
              "  movb %%al, (%%r12)\n");
        }
        break;
    }
    prev = srcval;
    num_prev = 1;
  }

  // generate lead-out code (frees the array)
  fprintf(out.get(), intel_syntax ? "  mov     rdi, r14\n" :
      "  mov     %%r14, %%rdi\n");
  fprintf(out.get(), "  call _free\n");
  fprintf(out.get(), intel_syntax ? "  pop     rbp" : "  pop     %%rbp\n");
  fprintf(out.get(), "  ret\n");

  // close the input and output files (which may be the assembler's stdin)
  source.reset();
  out.reset();

  if (assembler_process.get()) {
    assembler_process->wait();
  }
}
