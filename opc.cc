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

#include "AMD64Assembler.hh"
#include "CodeBuffer.hh"

using namespace std;


bool is_bf_command(char cmd) {
  return (cmd == '+') || (cmd == '-') || (cmd == '<') || (cmd == '>') ||
         (cmd == '[') || (cmd == ']') || (cmd == ',') || (cmd == '.');
}


void interpret(const char* filename, int optimize_level) {
  string code = load_file(filename);

  deque<string> memory_blocks;
  memory_blocks.emplace_back(1024 * 32, 0);
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
            memory_blocks.emplace_back(1024 * 32, 0);
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


void compile_and_run(const char* filename, size_t mem_size, int optimize_level,
    bool disassemble) {
  string code = load_file(filename);

  AMD64Assembler as;

  // r12 = memory ptr
  // r13 = end ptr (address of last valid byte)
  // rbx = current ptr
  // r14 = putchar
  // r15 = getchar

  // arguments: memory_ptr, memory_size

  // generate lead-in code
  as.write_push(Register::RBP);
  as.write_mov(rbp, rsp);
  as.write_push(Register::RBP);
  as.write_push(Register::RBP);
  as.write_push(Register::RBP);
  as.write_push(Register::RBP);
  as.write_push(Register::RBP);
  as.write_sub(rsp, 8); // alignment for function calls
  as.write_mov(r12, rdi);
  as.write_lea(Register::R13, MemoryReference(Register::RDI, 0, Register::RSI, 1));
  as.write_mov(rbx, rdi);
  as.write_mov(Register::R14, reinterpret_cast<int64_t>(&putchar));
  as.write_mov(Register::R15, reinterpret_cast<int64_t>(&getchar));

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
        as.write_label(string_printf("%zu_Increment", offset));
        if (num_same_opcode == 1) {
          as.write_inc(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_add(MemoryReference(Register::RBX, 0), num_same_opcode, OperandSize::Byte);
        }
        break;

      case '-':
        as.write_label(string_printf("%zu_Decrement", offset));
        if (num_same_opcode == 1) {
          as.write_dec(MemoryReference(Register::RBX, 0), OperandSize::Byte);
        } else {
          as.write_sub(MemoryReference(Register::RBX, 0), num_same_opcode, OperandSize::Byte);
        }
        break;

      case '>':
        as.write_label(string_printf("%zu_MoveRight", offset));

        if (optimize_level < 2) {
          as.write_cmp(rbx, r13);
          as.write_jge(string_printf("%zu_MoveRight_skip", offset));
        }
        if (num_same_opcode == 1) {
          as.write_inc(rbx);
        } else {
          as.write_add(rbx, num_same_opcode);
        }
        as.write_label(string_printf("%zu_MoveRight_skip", offset));
        break;

      case '<':
        as.write_label(string_printf("%zu_MoveLeft", offset));

        if (optimize_level < 2) {
          as.write_cmp(rbx, r12);
          as.write_jle(string_printf("%zu_MoveLeft_skip", offset));
        }
        if (num_same_opcode == 1) {
          as.write_dec(rbx);
        } else {
          as.write_sub(rbx, num_same_opcode);
        }
        as.write_label(string_printf("%zu_MoveLeft_skip", offset));
        break;

      case '[':
        as.write_label(string_printf("%zu_OpenBrace", offset));
        for (; num_same_opcode > 0; num_same_opcode--) {
          jump_offsets.emplace_back(offset - num_same_opcode);
          as.write_cmp(MemoryReference(Register::RBX, 0), 0, OperandSize::Byte);
          as.write_je(string_printf("jump_%zu_end", offset - num_same_opcode));
          as.write_label(string_printf("jump_%zu_begin", offset - num_same_opcode));
        }
        break;

      case ']':
        as.write_label(string_printf("%zu_CloseBrace", offset));
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
        as.write_label(string_printf("%zu_Output", offset));
        for (; num_same_opcode > 0; num_same_opcode--) {
          as.write_movzx8(Register::RDI, MemoryReference(Register::RBX, 0));
          as.write_call(r14);
        }
        break;

      case ',':
        as.write_label(string_printf("%zu_Input", offset));
        for (; num_same_opcode > 0; num_same_opcode--) {
          as.write_call(r15);
          as.write_mov(MemoryReference(Register::RBX, 0), al, OperandSize::Byte);
        }
        break;
    }
    prev_opcode = opcode;
    num_same_opcode = 1;
  }

  // generate lead-out code
  as.write_add(rsp, 8);
  as.write_pop(Register::R15);
  as.write_pop(Register::R14);
  as.write_pop(Register::R13);
  as.write_pop(Register::R12);
  as.write_pop(Register::RBX);
  as.write_pop(Register::RBP);
  as.write_ret();

  CodeBuffer buf;

  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  string data = as.assemble(patch_offsets, &compiled_labels);
  void (*function)(void* mem, size_t size) = reinterpret_cast<void(*)(void*, size_t)>(
      buf.append(data, &patch_offsets));

  string memory(mem_size, '\0');
  function(const_cast<char*>(memory.data()), memory.size());
}


void compile(const char* input_filename, const char* output_filename,
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


int main(int argc, char* argv[]) {

  int optimize_level = 1;
  int mem_size = 1048576;
  int num_bad_options = 0;
  bool skip_assembly = false;
  bool intel_syntax = false;
  bool execute = false;
  bool disassemble = false;

  int x;
  const char* input_filename = NULL;
  const char* output_filename = NULL;
  for (x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1]) {
      if (argv[x][1] == 'm') {
        mem_size = atoi(&argv[x][2]);
      } else if (argv[x][1] == 'O') {
        optimize_level = atoi(&argv[x][2]);
      } else if (argv[x][1] == 's') {
        skip_assembly = true;
      } else if (argv[x][1] == 'i') {
        skip_assembly = true;
        intel_syntax = true;
      } else if (argv[x][1] == 'x') {
        execute = true;
      } else if (argv[x][1] == 'd') {
        disassemble = true;
      } else {
        fprintf(stderr, "unknown command-line option: %s\n", argv[x]);
        num_bad_options++;
      }
    } else {
      if (input_filename) {
        if (output_filename) {
          fprintf(stderr, "too many filenames\n");
          num_bad_options++;
        } else {
          output_filename = argv[x];
        }
      } else {
        input_filename = argv[x];
      }
    }
  }

  if (!input_filename) {
    fprintf(stderr, "no input file\n");
    num_bad_options++;
  }

  if (num_bad_options) {
    fprintf(stderr, "\
to interpret code: %s [-ON] input_file\n\
to execute code: %s -x [-mX] [-ON] input_file\n\
to compile code: %s [-mX] [-ON] [-s|-i] input_file output_file\n\
\n\
options:\n\
  -mX sets memory size for program (default 1M)\n\
  -O sets optimization level (0-2) (default 1)\n\
  -s skips assembly step (output will be amd64 assembly code, at&t syntax)\n\
  -i skips assembly step (output will be amd64 assembly code, intel syntax)\n\
", argv[0], argv[0], argv[0]);
    return 1;
  }

  try {
    if (!output_filename) {
      if (execute) {
        compile_and_run(input_filename, mem_size, optimize_level, disassemble);
      } else {
        interpret(input_filename, optimize_level);
      }
    } else {
      compile(input_filename, output_filename, mem_size, optimize_level,
          skip_assembly, intel_syntax);
    }
  } catch (const exception& e) {
    fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
