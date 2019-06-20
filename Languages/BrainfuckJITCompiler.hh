#pragma once

#include <stddef.h>


void bf_execute(const char* filename, size_t mem_size, int optimize_level,
    size_t expansion_size, bool disassemble);
