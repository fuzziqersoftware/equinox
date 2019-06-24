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
#include "Languages/BrainfuckInterpreter.hh"
#include "Languages/BrainfuckJITCompiler.hh"
#include "Languages/MalbolgeInterpreter.hh"

using namespace std;



enum class Behavior {
  Interpret = 0,
  Execute = 1,
};

enum class Language {
  Automatic = 0,
  Brainfuck,
  Befunge,
  Malbolge,
};



int main(int argc, char* argv[]) {

  Language language = Language::Automatic;
  int optimize_level = 2;
  uint8_t dimensions = 2;
  size_t expansion_size = 0x10000; // 64KB
  size_t num_bad_options = 0;
  bool verbose = false;
  bool assembly = false;
  bool enable_debug_opcode = false;
  bool single_step = false;
  set<Position> befunge_breakpoints;
  Behavior behavior = Behavior::Execute;
  const char* input_filename = NULL;

  int x;
  for (x = 1; x < argc; x++) {

    // all-language options
    if (!strcmp(argv[x], "--verbose")) {
      verbose = true;
    } else if (!strcmp(argv[x], "--show-assembly")) {
      assembly = true;

    // mode selection
    } else if (!strcmp(argv[x], "--interpret")) {
      behavior = Behavior::Interpret;
    } else if (!strcmp(argv[x], "--execute")) {
      behavior = Behavior::Execute;

    // language selection
    } else if (!strcmp(argv[x], "--automatic")) {
      language = Language::Automatic;
    } else if (!strcmp(argv[x], "--brainfuck")) {
      language = Language::Brainfuck;
    } else if (!strcmp(argv[x], "--befunge")) {
      language = Language::Befunge;
    } else if (!strcmp(argv[x], "--malbolge")) {
      language = Language::Malbolge;

    // brainfuck options
    } else if (!strncmp(argv[x], "--memory-expansion-size=", 24)) {
      expansion_size = atoi(&argv[x][14]);
    } else if (!strncmp(argv[x], "--optimize-level=", 17)) {
      optimize_level = atoi(&argv[x][17]);

    // befunge options
    } else if (!strncmp(argv[x], "--dimensions=", 13)) {
      dimensions = atoi(&argv[x][13]);
    } else if (!strcmp(argv[x], "--single-step")) {
      single_step = true;
    } else if (!strncmp(argv[x], "--breakpoint=", 13)) {
      vector<string> parts = split(&argv[x][13], ',');
      if (parts.empty() || parts.size() > 3) {
        num_bad_options++;
      }
      int64_t x = strtoull(parts[0].c_str(), NULL, 0);
      int64_t y = (parts.size() > 1) ? strtoull(parts[1].c_str(), NULL, 0) : 0;
      int64_t z = (parts.size() > 2) ? strtoull(parts[2].c_str(), NULL, 0) : 0;
      befunge_breakpoints.emplace(x, y, z, 0, 0, 0);
    } else if (!strncmp(argv[x], "--dimensions=", 13)) {
      dimensions = atoi(&argv[x][13]);
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
    fprintf(stderr, "equinox: no input file\n");
    num_bad_options++;
  }

  if (num_bad_options) {
    fprintf(stderr, "\n\
Usage: %s [options] program_file\n\
\n\
Languages:\n\
  --automatic\n\
      Automatically detect the language based on the filename (default).\n\
  --befunge\n\
      Run the file as Funge-98.\n\
  --brainfuck\n\
      Run the file as Brainfuck.\n\
  --malbolge\n\
      Run the file as Malbolge.\n\
\n\
Modes:\n\
  --interpret\n\
      Run the code under an interpreter.\n\
  --execute\n\
      Compile the code to AMD64 assembly and run it (default).\n\
\n\
Options for all languages:\n\
  --show-assembly\n\
      In execute mode, output the compiled code\'s disassembly before running.\n\
      No effect in interpret mode.\n\
\n\
Brainfuck-specific options:\n\
  --memory-expansion-size=num_bytes\n\
      Sets the size by which the memory space is expanded when the program\n\
      accesses beyond the end (default 64KB).\n\
  --optimize-level=level\n\
      Sets the optimization level. Probably you want 2 (default).\n\
      Level 0: Translate every opcode directly into a sequence of instructions.\n\
      Level 1: Collapse repeated opcodes into more efficient instructions.\n\
      Level 2: Collapse common loops into more efficient instructions.\n\
\n\
Funge-98-specific options:\n\
  --dimensions=num\n\
      Chooses the sublanguage to use. 1 for Unefunge, 2 for Befunge (default),\n\
      3 for Trefunge.\n\
  --single-step\n\
      Enable single-step debugging at program start time.\n\
  --breakpoint=x[,y[,z]]\n\
      Enter interactive debugging when execution hits this location.\n\
      This option may be given multiple times.\n\
\n\
Malbolge runs only in interpret mode. There are no language-specific options.\n\
", argv[0]);
    return 1;
  }

  if (language == Language::Automatic) {
    if (ends_with(input_filename, ".b")) {
      language = Language::Brainfuck;
    } else if (ends_with(input_filename, ".bf")) {
      language = Language::Befunge;
    } else if (ends_with(input_filename, ".b98")) {
      language = Language::Befunge;
    } else if (ends_with(input_filename, ".mal")) {
      language = Language::Malbolge;
    } else {
      fprintf(stderr, "equinox: cannot detect language\n");
      return 1;
    }
  }

  try {
    if (language == Language::Brainfuck) {
      if (behavior == Behavior::Interpret) {
        bf_interpret(input_filename, expansion_size);
      } else if (behavior == Behavior::Execute) {
        bf_execute(input_filename, expansion_size, optimize_level,
            expansion_size, assembly);
      }
    } else if (language == Language::Befunge) {
      if (behavior == Behavior::Interpret) {
        befunge_interpret(input_filename, dimensions, enable_debug_opcode);
      } else if (behavior == Behavior::Execute) {
        using DF = BefungeJITCompiler::DebugFlag;
        uint64_t debug_flags =
            (assembly ? (DF::ShowCompilationEvents | DF::ShowAssembledCells) : 0) |
            (enable_debug_opcode ? DF::EnableStackPrintOpcode : 0) |
            (single_step ? (DF::InteractiveDebug | DF::SingleStep) : 0) |
            (befunge_breakpoints.empty() ? 0 : DF::InteractiveDebug);
        BefungeJITCompiler c(input_filename, dimensions, debug_flags);
        for (const auto& pos : befunge_breakpoints) {
          c.set_breakpoint(pos);
        }
        c.execute();
      }
    } else if (language == Language::Malbolge) {
      if (behavior == Behavior::Execute) {
        fprintf(stderr, "warning: malbolge compiler not implemented; falling back to interpreter\n");
      }
      malbolge_interpret(input_filename);
    }
  } catch (const exception& e) {
    fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
