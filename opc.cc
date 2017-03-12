#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <deque>
#include <memory>
#include <phosg/Filesystem.hh>
#include <phosg/Process.hh>
#include <string>

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


void compile(const char* input_filename, const char* output_filename,
    size_t mem_size, int optimize_level, bool skip_assembly) {

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

  // make the jump buffer
  vector<int> jump_ids;
  int latest_jump_id = 0;

  // generate lead-in code (allocates the array)
  fprintf(out.get(), ".globl _main\n");
  fprintf(out.get(), "_main:\n");
  fprintf(out.get(), "  push %%rbp\n");
  fprintf(out.get(), "  mov %%rsp, %%rbp\n");
  fprintf(out.get(), "  mov $%zu, %%rdi\n", mem_size);
  fprintf(out.get(), "  call _malloc\n");
  fprintf(out.get(), "  mov %%rax, %%r14\n");
  fprintf(out.get(), "  mov %%r14, %%r12\n");
  fprintf(out.get(), "  lea %zu(%%r14), %%r13\n", (mem_size - 1));

  // zero the memory block
  fprintf(out.get(), "0:\n");
  fprintf(out.get(), "  movb $0, (%%r12)\n");
  fprintf(out.get(), "  inc %%r12\n");
  fprintf(out.get(), "  cmp %%r13, %%r12\n");
  fprintf(out.get(), "  jle 0b\n");
  fprintf(out.get(), "  mov %%r14, %%r12\n");

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
        if (num_prev == 1) {
          fprintf(out.get(), "  incb (%%r12)\n");
        } else {
          fprintf(out.get(), "  addb $%d, (%%r12)\n", num_prev);
        }
        break;
      case '-':
        if (num_prev == 1) {
          fprintf(out.get(), "  decb (%%r12)\n");
        } else {
          fprintf(out.get(), "  subb $%d, (%%r12)\n", num_prev);
        }
        break;
      case '>':
        if (num_prev == 1) {
          if (optimize_level < 2) {
            fprintf(out.get(), "  cmp %%r13, %%r12\n");
            fprintf(out.get(), "  jge 1f\n");
          }
          fprintf(out.get(), "  inc %%r12\n");
        } else {
          if (optimize_level < 2) {
            fprintf(out.get(), "  mov %%r12, %%rax\n");
            fprintf(out.get(), "  add $%d, %%rax\n", num_prev);
            fprintf(out.get(), "  cmp %%r13, %%rax\n");
            fprintf(out.get(), "  jge 0f\n");
            fprintf(out.get(), "  mov %%rax, %%r12\n");
            fprintf(out.get(), "  jmp 1f\n");
            fprintf(out.get(), "0:\n");
            fprintf(out.get(), "  mov %%r13, %%r12\n");
          } else {
            fprintf(out.get(), "  add $%d, %%r12\n", num_prev);
          }
        }
        fprintf(out.get(), "1:\n");
        break;
      case '<':
        if (num_prev == 1) {
          if (optimize_level < 2) {
            fprintf(out.get(), "  cmp %%r14, %%r12\n");
            fprintf(out.get(), "  jle 1f\n");
          }
          fprintf(out.get(), "  dec %%r12\n");
        } else {
          if (optimize_level < 2) {
            fprintf(out.get(), "  mov %%r12, %%rax\n");
            fprintf(out.get(), "  sub $%d, %%rax\n", num_prev);
            fprintf(out.get(), "  cmp %%r14, %%rax\n");
            fprintf(out.get(), "  jle 0f\n");
            fprintf(out.get(), "  mov %%rax, %%r12\n");
            fprintf(out.get(), "  jmp 1f\n");
            fprintf(out.get(), "0:\n");
            fprintf(out.get(), "  mov %%r14, %%r12\n");
          } else {
            fprintf(out.get(), "  sub $%d, %%r12\n", num_prev);
          }
        }
        fprintf(out.get(), "1:\n");
        break;
      case '[':
        for (; num_prev > 0; num_prev--) {
          jump_ids.emplace_back(latest_jump_id++);
          fprintf(out.get(), "  cmpb $0, (%%r12)\n");
          fprintf(out.get(), "  je jump_%d_end\n", jump_ids.back());
          fprintf(out.get(), "jump_%d_begin:\n", jump_ids.back());
        }
        break;
      case ']':
        for (; num_prev > 0; num_prev--) {
          if (jump_ids.empty()) {
            throw runtime_error("unbalanced braces");
          }
          fprintf(out.get(), "  cmpb $0, (%%r12)\n");
          fprintf(out.get(), "  jne jump_%d_begin\n", jump_ids.back());
          fprintf(out.get(), "jump_%d_end:\n", jump_ids.back());
          jump_ids.pop_back();
        }
        break;
      case '.':
        for (; num_prev > 0; num_prev--) {
          fprintf(out.get(), "  movzbq (%%r12), %%rdi\n");
          fprintf(out.get(), "  call _putchar\n");
        }
        break;
      case ',':
        for (; num_prev > 0; num_prev--) {
          fprintf(out.get(), "  call _getchar\n");
          fprintf(out.get(), "  movb %%al, (%%r12)\n");
        }
        break;
    }
    prev = srcval;
    num_prev = 1;
  }

  // generate lead-out code (frees the array)
  fprintf(out.get(), "  mov %%r14, %%rdi\n");
  fprintf(out.get(), "  call _free\n");
  fprintf(out.get(), "  pop %%rbp\n");
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
  int skip_assembly = 0;

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
        skip_assembly = 1;
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
    fprintf(stderr, "usage: %s [-mX] [-ON] [-s] input_file [output_file]\n", argv[0]);
    fprintf(stderr, "  -mX sets memory size for program (default 1M)\n");
    fprintf(stderr, "  -O sets optimization level (0-2) (default 1)\n");
    fprintf(stderr, "  -s skips assembly step (output will be amd64 assembly code)\n");
    fprintf(stderr, "  if no output filename is given, the code is interpreted instead\n");
    return 1;
  }

  try {
    if (!output_filename) {
      interpret(input_filename, optimize_level);
    } else {
      compile(input_filename, output_filename, mem_size, optimize_level,
          skip_assembly);
    }
  } catch (const exception& e) {
    fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
