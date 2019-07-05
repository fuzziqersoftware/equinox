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

#include <libamd64/AMD64Assembler.hh>
#include <libamd64/CodeBuffer.hh>

#include "Befunge.hh"
#include "Common.hh"



class BefungeJITCompiler {
public:
  explicit BefungeJITCompiler(const std::string& filename,
      uint8_t dimensions = 2, uint64_t debug_flags = 0);
  ~BefungeJITCompiler() = default;

  void set_breakpoint(const Position& pos);

  void execute();

private:

  void check_dimensions(uint8_t required_dimensions, const Position& where,
      int16_t opcode) const;

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
  void write_jump_to_cell(AMD64Assembler& as, const Position& current_pos,
      const Position& next_pos);
  void write_jump_to_cell_unknown_alignment(AMD64Assembler& as,
      const Position& current_pos, const Position& next_pos);
  void write_jump_table(AMD64Assembler& as, const std::string& label_name,
      const Position& pos, const std::vector<Position>& positions);

  // the vector is [(reg, should_add_to_existing_value), ...]
  void write_load_storage_offset(AMD64Assembler& as,
      const std::vector<std::pair<MemoryReference, bool>>& target_regs);
  void write_throw_error(AMD64Assembler& as, const char* message);

  void add_common_object(const std::string& name, const void* o);
  MemoryReference common_object_reference(const std::string& name);

  MemoryReference storage_offset_reference(uint8_t dimension);
  MemoryReference end_of_last_stack_reference();

  static const void* dispatch_compile_cell(BefungeJITCompiler* c, const Position* pos);
  static const void* dispatch_get_cell_code(BefungeJITCompiler* c, const Position* pos);

  static int64_t dispatch_field_read(BefungeJITCompiler* c, int64_t x,
      int64_t y, int64_t z);
  static const void* dispatch_field_write(BefungeJITCompiler* c,
      int64_t return_position_token, int64_t x, int64_t y, int64_t z,
      int64_t value);

  static void dispatch_print_state(const int64_t* stack_top, size_t count,
      const Position* pos, int64_t* storage_offset, int64_t dimensions);

  void interactive_debug_hook(const Position& current_pos,
      const Position& next_pos, int64_t stack_top, int64_t r13,
      int64_t stack_end);
  static void dispatch_interactive_debug_hook(BefungeJITCompiler* c,
      const Position* current_pos, const Position* next_pos, int64_t stack_top,
      int64_t r13, int64_t stack_end);

  static void dispatch_throw_error(const char* error_string);

  uint8_t dimensions;
  uint64_t debug_flags;
  std::set<Position> breakpoint_positions;

  Field field;
  std::map<Position, CompiledCell> compiled_cells;

  int64_t next_token;
  std::unordered_map<int64_t, Position> token_to_position;

  std::vector<const void*> common_objects;
  std::unordered_map<std::string, size_t> common_object_index;

  CodeBuffer buf;
  const void* jump_return_40;
  const void* jump_return_38;
  const void* jump_return_8;
  const void* jump_return_0;
};
