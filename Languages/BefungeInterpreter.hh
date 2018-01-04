#pragma once

#include <string>


void befunge_interpret(const std::string& filename, uint8_t dimensions,
	bool enable_debug_opcode);
