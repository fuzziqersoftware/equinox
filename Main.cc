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

#include "Languages/BefungeInterpreter.hh"
#include "Languages/BefungeJITCompiler.hh"
#include "Languages/Brainfuck.hh"
#include "Languages/MalbolgeInterpreter.hh"

using namespace std;



enum Behavior {
  Interpret = 0,
  Compile = 1,
  Execute = 2,
};

enum Language {
  Brainfuck = 0,
  Befunge,
  Malbolge,
};



int main(int argc, char* argv[]) {

  Language language;
  int optimize_level = 1;
  size_t memory_size = 0x100000; // 1MB
  size_t expansion_size = 0x10000; // 64KB
  size_t num_bad_options = 0;
  bool assembly = false;
  bool intel_syntax = false;
  bool enable_debug_opcode = false;
  Behavior behavior = Behavior::Interpret;

  int x;
  const char* input_filename = NULL;
  const char* output_filename = NULL;
  for (x = 1; x < argc; x++) {

    // all-language options
    if (!strcmp(argv[x], "--assembly")) {
      assembly = true;
    } else if (!strcmp(argv[x], "--intel-syntax")) {
      intel_syntax = true;

    // mode selection
    } else if (!strcmp(argv[x], "--execute")) {
      behavior = Behavior::Execute;
    } else if (!strncmp(argv[x], "--output-filename=", 18)) {
      behavior = Behavior::Compile;
      output_filename = &argv[x][18];

    // language selection
    } else if (!strcmp(argv[x], "--brainfuck")) {
      language = Language::Brainfuck;
    } else if (!strcmp(argv[x], "--befunge")) {
      language = Language::Befunge;
    } else if (!strcmp(argv[x], "--malbolge")) {
      language = Language::Malbolge;

    // brainfuck options
    } else if (!strncmp(argv[x], "--memory-size=", 14)) {
      memory_size = atoi(&argv[x][14]);
      expansion_size = atoi(&argv[x][14]);
    } else if (!strncmp(argv[x], "--optimize-level=", 17)) {
      optimize_level = atoi(&argv[x][17]);

    // befunge options
    } else if (!strcmp(argv[x], "--enable-debug-opcode")) {
      enable_debug_opcode = true;

    // positional arguments
    } else if (!input_filename) {
      input_filename = argv[x];
    } else {
      fprintf(stderr, "too many positional arguments given\n");
      num_bad_options++;
    }
  }

  if (!input_filename) {
    fprintf(stderr, "no input file\n");
    num_bad_options++;
  }

  if (num_bad_options) {
    fprintf(stderr, "\
To interpret code: %s [options] input_file\n\
To compile code: %s [options] input_file --output-filename=output_file\n\
To execute code: %s -x [options] input_file\n\
\n\
Options for all languages:\n\
  --assembly\n\
      In compile mode, output amd64 assembly code instead of an executable. In\n\
      execute mode, output the compiled code\'s disassembly before running it.\n\
  --intel-syntax\n\
      In compile mode, output code using Intel syntax instead of AT&T syntax.\n\
      No effect in execute mode (debugging output is always Intel syntax).\n\
\n\
Brainfuck can run in all modes. Options:\n\
  --memory-size=num_bytes\n\
      In compile mode, sets the overall memory size for the compiled program\n\
      (default 1048576). In interpret and execute mode, sets the size by which\n\
      the memory space is expanded when the program accesses beyond the end\n\
      (default 65536).\n\
  --optimize-level=level\n\
      Sets the optimization level. Probably you want 1 (default).\n\
      Level 0: Translate every opcode directly into a sequence of instructions.\n\
      Level 1: Collapse repeated opcodes into more efficient instructions.\n\
      Level 2: Eliminate memory boundary checks.\n\
\n\
Befunge runs only in execute or interpret mode. Options:\n\
  --enable-debug-opcode\n\
      Enable the stack debug opcode. With this flag, the opcode Y causes the\n\
      compiler to print the contents of the stack to stderr. Without this flag,\n\
      the opcode Y causes an error.\n\
", argv[0], argv[0], argv[0]);
    return 1;
  }

  try {
    if (language == Language::Brainfuck) {
      if (behavior == Behavior::Interpret) {
        bf_interpret(input_filename, expansion_size);
      } else if (behavior == Behavior::Compile) {
        bf_compile(input_filename, output_filename, memory_size, optimize_level,
            assembly, intel_syntax);
      } else if (behavior == Behavior::Execute) {
        bf_execute(input_filename, memory_size, optimize_level, expansion_size,
            assembly);
      }
    } else if (language == Language::Befunge) {
      if (behavior == Behavior::Interpret) {
        befunge_interpret(input_filename, enable_debug_opcode);
      } else if (behavior == Behavior::Compile) {
        throw logic_error("befunge compiler not implemented");
      } else if (behavior == Behavior::Execute) {
        uint64_t debug_flags = (assembly ? 2 : 0) | (enable_debug_opcode ? 1 : 0);
        BefungeJITCompiler(input_filename, debug_flags).execute();
      }
    } else if (language == Language::Malbolge) {
      if (behavior == Behavior::Interpret) {
        malbolge_interpret(input_filename);
      } else if (behavior == Behavior::Compile) {
        throw logic_error("malbolge compiler not implemented");
      } else if (behavior == Behavior::Execute) {
        throw logic_error("malbolge executor not implemented");
      }
    }
  } catch (const exception& e) {
    fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
