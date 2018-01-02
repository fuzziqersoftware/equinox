#include "BefungeInterpreter.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Strings.hh>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Befunge.hh"

using namespace std;



struct Stack {
  vector<int64_t> stack;

  void push(int64_t i) {
    this->stack.emplace_back(i);
  }

  int64_t pop() {
    if (this->stack.empty()) {
      return 0;
    }
    int64_t i = this->stack.back();
    this->stack.pop_back();
    return i;
  }

  size_t size() const {
    return this->stack.size();
  }

  int64_t at(size_t x) const {
    return this->stack.at(this->stack.size() - x - 1);
  }
};



void befunge_interpret(const string& filename, bool enable_debug_opcode) {
  Field field = Field::load(filename);

  Stack stack;
  Position pos(0, 0, Direction::Right);

  for (;;) {
    int16_t opcode = -1;
    try {
      opcode = field.get(pos.x, pos.y);
    } catch (const out_of_range&) { }

    switch (opcode) {
        case -1:
          // boundary cell. figure out what's missing and fix it
          throw invalid_argument(string_printf("boundary cell %zd %zd", pos.x, pos.y));
          break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          stack.push(opcode - '0');
          break;

        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
          stack.push(opcode - 'a' + 10);
          break;

        case '+':
          stack.push(stack.pop() + stack.pop());
          break;
        case '-': {
          int64_t a = stack.pop();
          int64_t b = stack.pop();
          stack.push(b - a);
          break;
        }
        case '*':
          stack.push(stack.pop() * stack.pop());
          break;
        case '/': {
          int64_t a = stack.pop();
          int64_t b = stack.pop();
          stack.push(b / a);
          break;
        }
        case '%': {
          int64_t a = stack.pop();
          int64_t b = stack.pop();
          stack.push(b % a);
          break;
        }

        case '!': // logical not
          stack.push(!stack.pop());
          break;

        case '`': {
          int64_t a = stack.pop();
          int64_t b = stack.pop();
          stack.push(b > a);
          break;
        }

        case '<': // move left
          pos.face(Direction::Left);
          break;
        case '>': // move right
          pos.face(Direction::Right);
          break;
        case '^': // move up
          pos.face(Direction::Up);
          break;
        case 'v': // move down
          pos.face(Direction::Down);
          break;
        case '[': // turn left
          pos.turn_left();
          break;
        case ']': // turn right
          pos.turn_right();
          break;

        case '?': // move randomly
          pos.face(static_cast<Direction>(rand() & 3));
          break;

        case '_': // right if zero, left if not
          pos.face(stack.pop() ? Direction::Left : Direction::Right);
          break;

        case '|': // down if zero, up if not
          pos.face(stack.pop() ? Direction::Up : Direction::Down);
          break;

        case '\"': { // push an entire string
          pos.move_forward();
          for (;;) {
            int16_t value = field.get(pos.x, pos.y);
            if (value == '\"') {
              break;
            }

            stack.push(value);
            pos.move_forward();
          }

          // pos now points to the terminal quote; we'll automatically move
          // forward after this loop
          break;
        }

        case ';': { // skip over a segment
          pos.move_forward();
          for (;;) {
            int16_t value = field.get(pos.x, pos.y);
            if (value == ';') {
              break;
            }
            pos.move_forward();
          }

          // pos now points to the terminal semicolon; we'll automatically move
          // forward after this loop
          break;
        }

        case ':': { // duplicate top of stack
          int64_t i = stack.pop();
          stack.push(i);
          stack.push(i);
          break;
        }

        case '\\': { // swap top 2 items on stack
          int64_t a = stack.pop();
          int64_t b = stack.pop();
          stack.push(a);
          stack.push(b);
          break;
        }

        case '$': // discard top of stack
          stack.pop();
          break;

        case '.': // pop and print as integer followed by space
          printf("%" PRId64 " ", stack.pop());
          break;

        case ',': // pop and print as ascii character
          putchar(stack.pop());
          break;

        case ' ': // skip this cell
          break;

        case '#': // skip this cell and next cell
          pos.move_forward();
          break;

        case 'p': { // write program space
          int64_t y = stack.pop();
          int64_t x = stack.pop();
          int64_t v = stack.pop();
          field.set(x, y, v);
          break;
        }

        case 'g': // read program space
          try {
            int64_t y = stack.pop();
            int64_t x = stack.pop();
            stack.push(field.get(x, y));
          } catch (const out_of_range&) {
            stack.push(' ');
          }
          break;

        case '&': { // push user-supplied number
          int64_t i;
          scanf("%" PRId64, &i);
          stack.push(i);
          break;
        }

        case '~': // push user-supplied character
          stack.push(getchar());
          break;

        case '@': // end program
          return;

        case 'Y': // stack debug opcode
          if (enable_debug_opcode) {
            fprintf(stderr, "[stack debug: %zu items; item 0 at top]\n", stack.size());
            for (size_t x = 0; x < stack.size(); x++) {
              int64_t item = stack.at(x);
              if (item >= 0x20 && item <= 0x7F) {
                fprintf(stderr, "item %zu: %" PRId64 " (0x%" PRIX64 ") (%c)\n", x, item,
                    item, static_cast<char>(item));
              } else {
                fprintf(stderr, "item %zu: %" PRId64 " (0x%" PRIX64 ")\n", x, item, item);
              }
            }
            break;
          }

        default:
          throw invalid_argument(string_printf(
              "can\'t interpret character %02hX '%c' at (%zd, %zd)", opcode,
              opcode, pos.x, pos.y));
    }
    pos.move_forward();
  }
}
