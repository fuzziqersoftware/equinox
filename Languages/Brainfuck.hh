#pragma once

#include <stddef.h>


void bf_interpret(const char* filename, size_t expansion_size);
void bf_execute(const char* filename, size_t mem_size, int optimize_level,
    size_t expansion_size, bool disassemble);
void bf_compile(const char* input_filename, const char* output_filename,
    size_t mem_size, int optimize_level, bool skip_assembly, bool intel_syntax);
