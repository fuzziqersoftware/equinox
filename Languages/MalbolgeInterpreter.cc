#include "MalbolgeInterpreter.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

using namespace std;



static uint16_t crz(uint16_t x, uint16_t y) {
  static const uint8_t table[9] = {1, 0, 0, 1, 0, 2, 2, 2, 1};
  uint16_t result = 0;
  for (size_t power = 1; power < 59049; power *= 3) {
    uint8_t x_trit = (x / power) % 3;
    uint8_t y_trit = (y / power) % 3;
    result += table[y_trit * 3 + x_trit] * power;
  }
  return result;
}

void malbolge_interpret(const string& filename) {
  string program = load_file(filename);
  u16string memory;
  memory.reserve(program.size());

  for (size_t x = 0; x < program.size(); x++) {
    char normalized_opcode = (program[x] + x) % 94;
    if ((normalized_opcode != 4) && (normalized_opcode != 5) &&
        (normalized_opcode != 23) && (normalized_opcode != 39) &&
        (normalized_opcode != 40) && (normalized_opcode != 62) &&
        (normalized_opcode != 68) && (normalized_opcode != 81)) {
      throw invalid_argument(string_printf("incorrect opcode at position %zu (%c)",
          x, program[x]));
    }
    memory.push_back(program[x]);
  }

  size_t a = 0, c = 0, d = 0;
  for (;;) {
    if (c >= memory.size()) {
      throw runtime_error("execution reached the end of memory");
    }

    uint16_t opcode = memory[c];
    uint8_t normalized_opcode = (opcode + c) % 94;

    switch (normalized_opcode) {
      case 4:
        c = program[d] - 1;
        if (c >= memory.size()) {
          throw runtime_error("jump beyond end of memory");
        }
        opcode = memory[c]; // needed for reencryption later
        break;
      case 5:
        putc(a & 0xFF, stdout);
        break;
      case 23:
        a = getchar();
        if (a == EOF) {
          a = 59048;
        }
        break;
      case 39:
        a = memory[d];
        a = a / 3 + ((a % 3) * (59049 / 3));
        memory[d] = a;
        break;
      case 40:
        d = memory[d];
        break;
      case 62:
        a = crz(a, memory[d]);
        memory[d] = a;
        break;
      case 68:
        break;
      case 81:
        return;
    }

    static const char* encoding_table = "5z]&gqtyfr$(we4{WP)H-Zn,[%\\3dL+Q;>U!pJS72FhOA1CB6v^=I_0/8|jsb9m<.TVac`uY*MK\'X~xDl}REokN:#?G\"i@";
    if (opcode >= 33 && opcode <= 126) {
      memory[c] = encoding_table[opcode - 33];
    }

    c = (c + 1) % 59049;
    d = (d + 1) % 59049;
  }
}
