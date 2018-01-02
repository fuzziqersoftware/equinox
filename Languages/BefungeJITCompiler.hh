#pragma once

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Assembler/AMD64Assembler.hh"
#include "../Assembler/CodeBuffer.hh"

#include "Befunge.hh"



class BefungeJITCompiler {
public:
  enum DebugFlag {
    EnableStackPrintOpcode = 1,
    ShowAssembledCells = 2,
  };

  explicit BefungeJITCompiler(const std::string& filename,
      uint64_t debug_flags = 0);
  ~BefungeJITCompiler() = default;

  void execute();

private:

  // TODO: recycle old buffer blocks by keeping them in a free list

  struct CompiledCell {
    // Position not included; it's the map key
    void* code;
    size_t code_size;
    size_t buffer_capacity;

    std::unordered_set<int64_t> next_position_tokens;
    std::set<Position> address_dependencies;

    CompiledCell();
    CompiledCell(void* code, size_t code_size);
    CompiledCell(const Position& dependency);
  };

  void compile_opcode(AMD64Assembler& as, const Position& pos, int16_t opcode);
  void compile_opcode_iterated(AMD64Assembler& as, const Position& iterator_pos,
      const Position& target_pos, int16_t opcode);
  const void* compile_cell(const Position& cell_pos, bool reset_cell = false);

  static void write_function_call(AMD64Assembler& as,
      const MemoryReference& function_ref, bool stack_aligned);
  void write_jump_to_cell(const Position& current_pos, AMD64Assembler& as,
      const Position& next_pos);
  void write_direction_jump_table(AMD64Assembler& as,
      const std::string& label_name, const Position& pos,
      const std::vector<Direction>& dirs);

  void add_common_object(const std::string& name, const void* o);
  MemoryReference common_object_reference(const std::string& name);

  static const void* dispatch_compile_cell(BefungeJITCompiler* c, ssize_t x,
      ssize_t y, Direction dir, bool stack_aligned);
  static const void* dispatch_get_cell_code(BefungeJITCompiler* c, ssize_t x,
      ssize_t y, Direction dir, bool stack_aligned);

  static int64_t dispatch_field_read(BefungeJITCompiler* c, ssize_t x,
      ssize_t y);
  static const void* dispatch_field_write(BefungeJITCompiler* c,
      int64_t return_position_token, ssize_t x, ssize_t y, int64_t value);

  static void dispatch_print_stack_contents(const int64_t* stack_top,
      size_t count);

  static void dispatch_throw_error(const char* error_string);

  uint64_t debug_flags;

  Field field;
  std::map<Position, CompiledCell> compiled_cells;

  int64_t next_token;
  std::unordered_map<int64_t, Position> token_to_position;

  std::vector<const void*> common_objects;
  std::unordered_map<std::string, size_t> common_object_index;

  CodeBuffer buf;
  const void* dispatch_compile_cell_ret_aligned;
  const void* dispatch_compile_cell_ret_misaligned;
};
