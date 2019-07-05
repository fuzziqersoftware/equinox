#pragma once

#include <stddef.h>

#include <libamd64/AMD64Assembler.hh>
#include <libamd64/CodeBuffer.hh>



class BrainfuckJITCompiler {
public:
  explicit BrainfuckJITCompiler(const std::string& filename, size_t mem_size,
      size_t cell_size, int optimize_level, size_t expansion_size,
      uint64_t debug_flags = 0);
  ~BrainfuckJITCompiler() = default;

  void execute();

private:
  std::pair<std::map<ssize_t, ssize_t>, size_t> get_mover_loop_info(
      size_t offset);
  void write_load_cell_value(AMD64Assembler& as, const MemoryReference& dest,
      const MemoryReference& src);

  std::string code;
  size_t expansion_size;
  size_t cell_size;
  OperandSize operand_size;
  int optimize_level;
  uint64_t debug_flags;

  CodeBuffer buf;

  static const std::unordered_map<size_t, OperandSize> cell_size_to_operand_size;
};
