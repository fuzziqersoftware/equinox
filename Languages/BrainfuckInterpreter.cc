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


void bf_interpret(const char* filename, size_t expansion_size, size_t cell_size) {
  if ((cell_size != 1) && (cell_size != 2) && (cell_size != 4) && (cell_size != 8)) {
    throw invalid_argument("cell size must be 1, 2, 4, or 8");
  }

  string code = load_file(filename);

  void* memory = calloc(expansion_size, cell_size);
  size_t memory_size = expansion_size;

  size_t pc = 0;
  size_t memory_offset = 0;
  while (pc < code.size()) {
    switch (code[pc]) {
      case '>':
        memory_offset++;
        if (memory_offset >= memory_size) {
          memory_size += expansion_size;
          memory = realloc(memory, memory_size * cell_size);
          memset(reinterpret_cast<uint8_t*>(memory) + ((memory_size - expansion_size) * cell_size),
              0, expansion_size * cell_size);
        }
        break;

      case '<':
        if (memory_offset) {
          memory_offset--;
        }
        break;

      case '+':
        if (cell_size == 1) {
          (*(reinterpret_cast<uint8_t*>(memory) + memory_offset))++;
        } else if (cell_size == 2) {
          (*(reinterpret_cast<uint16_t*>(memory) + memory_offset))++;
        } else if (cell_size == 4) {
          (*(reinterpret_cast<uint32_t*>(memory) + memory_offset))++;
        } else {
          (*(reinterpret_cast<uint64_t*>(memory) + memory_offset))++;
        }
        break;

      case '-':
        if (cell_size == 1) {
          (*(reinterpret_cast<uint8_t*>(memory) + memory_offset))--;
        } else if (cell_size == 2) {
          (*(reinterpret_cast<uint16_t*>(memory) + memory_offset))--;
        } else if (cell_size == 4) {
          (*(reinterpret_cast<uint32_t*>(memory) + memory_offset))--;
        } else {
          (*(reinterpret_cast<uint64_t*>(memory) + memory_offset))--;
        }
        break;

      case '.':
        char ch;
        if (cell_size == 1) {
          ch = *(reinterpret_cast<uint8_t*>(memory) + memory_offset);
        } else if (cell_size == 2) {
          ch = *(reinterpret_cast<uint16_t*>(memory) + memory_offset);
        } else if (cell_size == 4) {
          ch = *(reinterpret_cast<uint32_t*>(memory) + memory_offset);
        } else {
          ch = *(reinterpret_cast<uint64_t*>(memory) + memory_offset);
        }
        putchar(ch);
        break;

      case ',': {
        char ch = getchar();
        if (cell_size == 1) {
          *(reinterpret_cast<uint8_t*>(memory) + memory_offset) = ch;
        } else if (cell_size == 2) {
          *(reinterpret_cast<uint16_t*>(memory) + memory_offset) = ch;
        } else if (cell_size == 4) {
          *(reinterpret_cast<uint32_t*>(memory) + memory_offset) = ch;
        } else {
          *(reinterpret_cast<uint64_t*>(memory) + memory_offset) = ch;
        }
        break;
      }

      case '[': {
        uint64_t v;
        if (cell_size == 1) {
          v = *(reinterpret_cast<uint8_t*>(memory) + memory_offset);
        } else if (cell_size == 2) {
          v = *(reinterpret_cast<uint16_t*>(memory) + memory_offset);
        } else if (cell_size == 4) {
          v = *(reinterpret_cast<uint32_t*>(memory) + memory_offset);
        } else {
          v = *(reinterpret_cast<uint64_t*>(memory) + memory_offset);
        }

        if (v == 0) {
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
      }

      case ']': {
        uint64_t v;
        if (cell_size == 1) {
          v = *(reinterpret_cast<uint8_t*>(memory) + memory_offset);
        } else if (cell_size == 2) {
          v = *(reinterpret_cast<uint16_t*>(memory) + memory_offset);
        } else if (cell_size == 4) {
          v = *(reinterpret_cast<uint32_t*>(memory) + memory_offset);
        } else {
          v = *(reinterpret_cast<uint64_t*>(memory) + memory_offset);
        }

        if (v != 0) {
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
    }
    pc++;
  }
}
