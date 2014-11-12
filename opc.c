// build this file with:
// gcc -Wall opc.c -o opc
// then build BF programs with:
// ./opc $INPUT_BF_FILE [$OUTPUT_EXECUTABLE]

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int is_bf_command(int cmd) {
  return (cmd == '+') || (cmd == '-') || (cmd == '<') || (cmd == '>') ||
         (cmd == '[') || (cmd == ']') || (cmd == ',') || (cmd == '.');
}

// builds a Brainfuck program (outputs assembly code)
int main(int argc, char* argv[]) {

  int optimize = 1;
  int mem_size = 1048576;
  int num_bad_options = 0;
  int skip_assembly = 0;

  int x;
  const char* input_filename = NULL;
  const char* output_filename = NULL;
  const char* temp_filename = NULL;
  for (x = 1; x < argc; x++) {
    if (argv[x][0] == '-' && argv[x][1]) {
      if (argv[x][1] == 'm')
        mem_size = atoi(&argv[x][2]);
      else if (argv[x][1] == 'O')
        optimize = atoi(&argv[x][2]);
      else if (argv[x][1] == 's')
        skip_assembly = 1;
      else {
        fprintf(stderr, "opc: unknown command-line option: %s\n", argv[x]);
        num_bad_options++;
      }
    } else {
      if (input_filename) {
        if (output_filename) {
          fprintf(stderr, "opc: too many filenames\n");
          num_bad_options++;
        } else
          output_filename = argv[x];
      } else
        input_filename = argv[x];
    }
  }

  if (!input_filename) {
    fprintf(stderr, "opc: no input file\n");
    num_bad_options++;
  }

  if (num_bad_options) {
    fprintf(stderr, "opc: usage: %s [-mX] [-xN] [-s] input_file [output_file]\n", argv[0]);
    fprintf(stderr, "opc:   -mX sets memory size for program (default 1M)\n");
    fprintf(stderr, "opc:   -O sets optimization level (0-2) (default 1)\n");
    fprintf(stderr, "opc:   -s skips assembly step (output will be x86 assembly code)\n");
    return 1;
  }

  FILE *source, *out;
  if (!strcmp(input_filename, "-"))
    source = stdin;
  else {
    source = fopen(input_filename, "rt");
    if (!source) {
      printf("opc: failed to open \"%s\" for reading (%d)\n", input_filename, errno);
      return 2;
    }
  }

  if (!output_filename)
    output_filename = "a.out";

  if (!skip_assembly) {
    temp_filename = tmpnam(NULL);
    out = fopen(temp_filename, "wt");
    if (!out) {
      printf("opc: failed to open \"%s\" for writing (%d)\n", temp_filename, errno);
      return 2;
    }
  } else {
    out = fopen(output_filename, "wt");
    if (!out) {
      printf("opc: failed to open \"%s\" for writing (%d)\n", output_filename, errno);
      return 2;
    }
  }

  // make the jump buffer
  int num_jump_ids = 0;
  int num_jump_ids_alloced = 16;
  int latest_jump_id = 0;
  int* jump_ids = malloc(sizeof(int) * num_jump_ids_alloced);

  // generate lead-in code (allocates the array)
  fprintf(out, ".globl _main\n");
  fprintf(out, "_main:\n");
  fprintf(out, "  push %%rbp\n");
  fprintf(out, "  mov %%rsp, %%rbp\n");
  fprintf(out, "  mov $%d, %%rdi\n", mem_size);
  fprintf(out, "  call _malloc\n");
  fprintf(out, "  mov %%rax, %%r14\n");
  fprintf(out, "  mov %%r14, %%r12\n");
  fprintf(out, "  lea %d(%%r14), %%r13\n", (mem_size - 1));

  // zero the memory block
  fprintf(out, "0:\n");
  fprintf(out, "  movb $0, (%%r12)\n");
  fprintf(out, "  inc %%r12\n");
  fprintf(out, "  cmp %%r13, %%r12\n");
  fprintf(out, "  jle 0b\n");
  fprintf(out, "  mov %%r14, %%r12\n");

  // r12 = current ptr
  // r13 = end ptr
  // r14 = begin ptr

  // generate assembly
  char prev = 1;
  int numPrev = 1;
  while (prev != EOF) {
    int srcval = fgetc(source);
    if (srcval != EOF && !is_bf_command(srcval))
      continue;
    if (optimize) {
      if (srcval == prev) {
        numPrev++;
        continue;
      }
    }
    switch (prev) {
      case '+':
        if (numPrev == 1)
          fprintf(out, "  incb (%%r12)\n");
        else
          fprintf(out, "  addb $%d, (%%r12)\n", numPrev);
        break;
      case '-':
        if (numPrev == 1)
          fprintf(out, "  decb (%%r12)\n");
        else
          fprintf(out, "  subb $%d, (%%r12)\n", numPrev);
        break;
      case '>':
        if (numPrev == 1) {
          if (optimize < 2) {
            fprintf(out, "  cmp %%r13, %%r12\n");
            fprintf(out, "  jge 1f\n");
          }
          fprintf(out, "  inc %%r12\n");
        } else {
          if (optimize < 2) {
            fprintf(out, "  mov %%r12, %%rax\n");
            fprintf(out, "  add $%d, %%rax\n", numPrev);
            fprintf(out, "  cmp %%r13, %%rax\n");
            fprintf(out, "  jge 0f\n");
            fprintf(out, "  mov %%rax, %%r12\n");
            fprintf(out, "  jmp 1f\n");
            fprintf(out, "0:\n");
            fprintf(out, "  mov %%r13, %%r12\n");
          } else
            fprintf(out, "  add $%d, %%r12\n", numPrev);
        }
        fprintf(out, "1:\n");
        break;
      case '<':
        if (numPrev == 1) {
          if (optimize < 2) {
            fprintf(out, "  cmp %%r14, %%r12\n");
            fprintf(out, "  jle 1f\n");
          }
          fprintf(out, "  dec %%r12\n");
        } else {
          if (optimize < 2) {
            fprintf(out, "  mov %%r12, %%rax\n");
            fprintf(out, "  sub $%d, %%rax\n", numPrev);
            fprintf(out, "  cmp %%r14, %%rax\n");
            fprintf(out, "  jle 0f\n");
            fprintf(out, "  mov %%rax, %%r12\n");
            fprintf(out, "  jmp 1f\n");
            fprintf(out, "0:\n");
            fprintf(out, "  mov %%r14, %%r12\n");
          } else
            fprintf(out, "  sub $%d, %%r12\n", numPrev);
        }
        fprintf(out, "1:\n");
        break;
      case '[':
        for (; numPrev > 0; numPrev--) {
          if (num_jump_ids == num_jump_ids_alloced) {
            num_jump_ids_alloced *= 2;
            jump_ids = realloc(jump_ids, sizeof(int) * num_jump_ids_alloced);
          }
          jump_ids[num_jump_ids] = latest_jump_id++;
          num_jump_ids++;

          fprintf(out, "  cmpb $0, (%%r12)\n");
          fprintf(out, "  je jump_%d_end\n", jump_ids[num_jump_ids - 1]);
          fprintf(out, "jump_%d_begin:\n", jump_ids[num_jump_ids - 1]);
        }
        break;
      case ']':
        for (; numPrev > 0; numPrev--) {
          if (num_jump_ids <= 0) {
            fprintf(stderr, "error: unbalanced loop braces\n");
            return (-1);
          }
          fprintf(out, "  cmpb $0, (%%r12)\n");
          fprintf(out, "  jne jump_%d_begin\n", jump_ids[num_jump_ids - 1]);
          fprintf(out, "jump_%d_end:\n", jump_ids[num_jump_ids - 1]);
          num_jump_ids--;
        }
        break;
      case '.':
        for (; numPrev > 0; numPrev--) {
          fprintf(out, "  movzbq (%%r12), %%rdi\n");
          fprintf(out, "  call _putchar\n");
        }
        break;
      case ',':
        for (; numPrev > 0; numPrev--) {
          fprintf(out, "  call _getchar\n");
          fprintf(out, "  movb %%al, (%%r12)\n");
        }
        break;
    }
    prev = srcval;
    numPrev = 1;
  }

  // generate lead-out code (frees the array)
  fprintf(out, "  mov %%r14, %%rdi\n");
  fprintf(out, "  call _free\n");
  fprintf(out, "  pop %%rbp\n");
  fprintf(out, "  ret\n");

  fclose(out);

  int retcode = 0;
  if (!skip_assembly) {
    // compile that sucker
    char* command;
    asprintf(&command, "gcc -o %s -m64 -x assembler-with-cpp %s", output_filename, temp_filename);
    if (system(command)) {
      fprintf(stderr, "opc: failed to assemble program; command: %s\n", command);
      retcode = 3;
    }
    free(command);
    unlink(temp_filename);
  }

  return retcode;
}
