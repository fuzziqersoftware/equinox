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



BefungeInterpreter::BefungeInterpreter(const string& filename,
    uint8_t dimensions) : pos(0, 0, 0, 1, 0, 0), dimensions(dimensions) {
  this->ss.push(Stack<int64_t>());
  this->s = &this->ss.at(0);
  this->field = Field::load(filename);
}



void BefungeInterpreter::execute() {
  for (;;) {
    int16_t opcode = -1;
    try {
      opcode = this->field.get(pos.x, pos.y, pos.z);
    } catch (const out_of_range&) { }

    this->execute_opcode(opcode);
  }
}


void BefungeInterpreter::execute_opcode(int16_t opcode) {
  switch (opcode) {
    case -1:
      // boundary cell. figure out what's missing and fix it
      throw invalid_argument(string_printf(
          "interpreter executed boundary cell at x=%zd y=%zd z=%zd",
          pos.x, pos.y, pos.z));

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
      this->s->push(opcode - '0');
      break;

    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
      this->s->push(opcode - 'a' + 10);
      break;

    case '+':
      this->s->push(this->s->pop() + this->s->pop());
      break;
    case '-': {
      int64_t a = this->s->pop();
      int64_t b = this->s->pop();
      this->s->push(b - a);
      break;
    }
    case '*':
      this->s->push(this->s->pop() * this->s->pop());
      break;
    case '/': {
      int64_t a = this->s->pop();
      int64_t b = this->s->pop();
      if (a == 0) {
        this->s->push(0);
      } else {
        this->s->push(b / a);
      }
      break;
    }
    case '%': {
      int64_t a = this->s->pop();
      int64_t b = this->s->pop();
      if (a == 0) {
        this->s->push(0);
      } else {
        this->s->push(b % a);
      }
      break;
    }

    case '!': // logical not
      this->s->push(!this->s->pop());
      break;

    case '`': {
      int64_t a = this->s->pop();
      int64_t b = this->s->pop();
      this->s->push(b > a);
      break;
    }

    case '<': // move left
      pos.face(-1, 0, 0);
      break;
    case '>': // move right
      pos.face(1, 0, 0);
      break;
    case '^': // move up
      if (this->dimensions < 2) {
        throw runtime_error("opcode only implemented in 2 or more dimensions");
      }
      pos.face(0, -1, 0);
      break;
    case 'v': // move down
      if (this->dimensions < 2) {
        throw runtime_error("opcode only implemented in 2 or more dimensions");
      }
      pos.face(0, 1, 0);
      break;
    case '[': // turn left
      if (this->dimensions < 2) {
        throw runtime_error("opcode only implemented in 2 or more dimensions");
      }
      pos.turn_left();
      break;
    case ']': // turn right
      if (this->dimensions < 2) {
        throw runtime_error("opcode only implemented in 2 or more dimensions");
      }
      pos.turn_right();
      break;

    case '?': // move randomly
      switch (rand() % (this->dimensions * 2)) {
        case 0:
          pos.face(-1, 0, 0);
          break;
        case 1:
          pos.face(1, 0, 0);
          break;
        case 2:
          pos.face(0, -1, 0);
          break;
        case 3:
          pos.face(0, 1, 0);
          break;
        case 4:
          pos.face(0, 0, 1);
          break;
        case 5:
          pos.face(0, 0, -1);
          break;
      }
      break;

    case '_': // right if zero, left if not
      pos.face(this->s->pop() ? -1 : 1, 0, 0);
      break;

    case '|': // down if zero, up if not
      if (this->dimensions < 2) {
        throw runtime_error("opcode only implemented in 2 or more dimensions");
      }
      pos.face(0, this->s->pop() ? -1 : 1, 0);
      break;

    case '\"': { // push an entire string
      pos.move_forward();
      for (;;) {
        int16_t value = this->field.get(pos.x, pos.y, pos.z);
        if (value == '\"') {
          break;
        }

        this->s->push(value);
        pos.move_forward();
      }

      // pos now points to the terminal quote; we'll automatically move
      // forward after this loop
      break;
    }

    case ';': { // skip over a segment
      pos.move_forward();
      for (;;) {
        int16_t value = this->field.get(pos.x, pos.y, pos.z);
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
      int64_t i = this->s->pop();
      this->s->push(i);
      this->s->push(i);
      break;
    }

    case '\\': { // swap top 2 items on stack
      int64_t a = this->s->pop();
      int64_t b = this->s->pop();
      this->s->push(a);
      this->s->push(b);
      break;
    }

    case '$': // discard top of stack
      this->s->pop();
      break;

    case '.': // pop and print as integer followed by space
      printf("%" PRId64 " ", this->s->pop());
      break;

    case ',': // pop and print as ascii character
      putchar(this->s->pop());
      break;

    case ' ': // skip this cell
      break;

    case '#': // skip this cell and next cell
      pos.move_forward();
      break;

    case 'p': { // write program space
      int64_t z = (dimensions > 2) ? this->s->pop() : 0;
      int64_t y = (dimensions > 1) ? this->s->pop() : 0;
      int64_t x = this->s->pop();
      int64_t v = this->s->pop();
      this->field.set(x, y, z, v);
      break;
    }

    case 'g': // read program space
      try {
        int64_t z = (dimensions > 2) ? this->s->pop() : 0;
        int64_t y = (dimensions > 1) ? this->s->pop() : 0;
        int64_t x = this->s->pop();
        this->s->push(this->field.get(x, y, z));
      } catch (const out_of_range&) {
        this->s->push(' ');
      }
      break;

    case '&': { // push user-supplied number
      int64_t i;
      scanf("%" PRId64, &i);
      this->s->push(i);
      break;
    }

    case '~': // push user-supplied character
      this->s->push(getchar());
      break;

    case '@': // end program
      return;

    // TODO: implement stack debug opcode (Y)

    default:
      throw invalid_argument(string_printf(
          "can\'t interpret character %02hX '%c' at (%zd, %zd)", opcode,
          opcode, pos.x, pos.y));
  }
  pos.move_forward();
}
