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

#include "Languages/Common.hh"
#include "Languages/BefungeInterpreter.hh"
#include "Languages/BefungeJITCompiler.hh"
#include "Languages/BrainfuckInterpreter.hh"
#include "Languages/BrainfuckJITCompiler.hh"
#include "Languages/MalbolgeInterpreter.hh"
#include "Languages/DeadfishInterpreter.hh"
#include "Languages/DeadfishJITCompiler.hh"

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
  Deadfish,
};



int main(int argc, char* argv[]) {

  Language language = Language::Automatic;
  int optimize_level = 2;
  uint8_t dimensions = 2;
  size_t cell_size = 8;
  size_t expansion_size = 0x2000; // 64KB (8192 cells)
  size_t num_bad_options = 0;
  bool verbose = false;
  bool assembly = false;
  bool single_step = false;
  set<Position> befunge_breakpoints;
  bool deadfish_ascii = false;
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
    } else if (!strcmp(argv[x], "--language=automatic")) {
      language = Language::Automatic;
    } else if (!strcmp(argv[x], "--language=brainfuck")) {
      language = Language::Brainfuck;
    } else if (!strcmp(argv[x], "--language=funge-98")) {
      language = Language::Befunge;
    } else if (!strcmp(argv[x], "--language=befunge")) {
      language = Language::Befunge;
    } else if (!strcmp(argv[x], "--language=malbolge")) {
      language = Language::Malbolge;
    } else if (!strcmp(argv[x], "--language=deadfish")) {
      language = Language::Deadfish;

    // brainfuck options
    } else if (!strncmp(argv[x], "--cell-size=", 12)) {
      cell_size = atoi(&argv[x][12]);
    } else if (!strncmp(argv[x], "--memory-expansion-size=", 24)) {
      expansion_size = atoi(&argv[x][24]);
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

    // deadfish options
    } else if (!strcmp(argv[x], "--ascii")) {
      deadfish_ascii = true;

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
  --language=automatic\n\
      Automatically detect the language based on the filename (default).\n\
  --language=funge=98 or --language=befunge\n\
      Run the file as Funge-98.\n\
  --language=brainfuck\n\
      Run the file as Brainfuck.\n\
  --language=malbolge\n\
      Run the file as Malbolge.\n\
  --language=deadfish\n\
      Run the file as Deadfish.\n\
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
  --cell-size=size\n\
      Set the number of bytes per cell. May be 1, 2, 4, or 8 (default).\n\
      If a program hangs during execution, it may rely on integer overflow\n\
      semantics which depend on the cell size - try a smaller size.\n\
  --memory-expansion-size=num_cells\n\
      Sets the number of cells by which the memory space is expanded when the\n\
      program accesses beyond the end (default 8192). Each cell is 8 bytes.\n\
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
\n\
Deadfish-specific options:\n\
  --ascii\n\
      Output the ASCII character corresponding to each output number instead of\n\
      the number\'s decimal representation.\n\
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
    } else if (ends_with(input_filename, ".df")) {
      language = Language::Deadfish;
    } else {
      fprintf(stderr, "equinox: cannot detect language\n");
      return 1;
    }
  }

  uint64_t debug_flags =
      (assembly ? (DebugFlag::ShowCompilationEvents | DebugFlag::ShowAssembly) : 0);

  try {
    if (language == Language::Brainfuck) {
      if (behavior == Behavior::Interpret) {
        bf_interpret(input_filename, expansion_size, cell_size);
      } else if (behavior == Behavior::Execute) {
        BrainfuckJITCompiler c(input_filename, expansion_size, cell_size,
            optimize_level, expansion_size, debug_flags);
        c.execute();
      }

    } else if (language == Language::Befunge) {
      if (behavior == Behavior::Interpret) {
        BefungeInterpreter i(input_filename, dimensions);
        i.execute();
      } else if (behavior == Behavior::Execute) {
        debug_flags |=
            (single_step ? (DebugFlag::InteractiveDebug | DebugFlag::SingleStep) : 0) |
            (befunge_breakpoints.empty() ? 0 : DebugFlag::InteractiveDebug);
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

    } else if (language == Language::Deadfish) {
      if (behavior == Behavior::Interpret) {
        DeadfishInterpreter i(input_filename, deadfish_ascii);
        i.execute();
      } else if (behavior == Behavior::Execute) {
        DeadfishJITCompiler c(input_filename, deadfish_ascii, debug_flags);
        c.execute();
      }
    }

  } catch (const exception& e) {
    fprintf(stderr, "failed: %s\n", e.what());
    return 1;
  }
  return 0;
}
