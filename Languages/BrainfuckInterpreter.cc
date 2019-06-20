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
