#include "BefungeJITCompiler.hh"

using namespace std;



// the compiled code follows the system v calling convention, with the following
// special registers:
// r12 = common object ptr
// r13 = stack end ptr - 8 (address of the last item on the stack). we do this
//       so comparisons can be as useful as possible; if rsp < r13, there are
//       two or more items on the stack, rsp == r13 means exactly one item,
//       rsp > r13 means the stack is empty.



BefungeJITCompiler::BefungeJITCompiler(const string& filename,
    uint8_t dimensions, uint64_t debug_flags) : dimensions(dimensions),
    debug_flags(debug_flags), field(Field::load(filename)), next_token(1) {

  // initialiy, all cells are just calls to the compiler. but watch out: these
  // compiler calls might overwrite the cell that called them, so they can't
  // call the compiler normally - instead, they return to this fragment that
  // makes them "return" to the destination cell
  {
    AMD64Assembler as;
    as.write_label("jump_return_40");
    as.write_add(rsp, 0x40);
    as.write_jmp(rax);
    as.write_label("jump_return_38");
    as.write_add(rsp, 0x38);
    as.write_jmp(rax);
    as.write_label("jump_return_8");
    as.write_add(rsp, 0x08);
    as.write_label("jump_return_0");
    as.write_jmp(rax);

    unordered_set<size_t> patch_offsets;
    multimap<size_t, string> label_offsets;
    string data = as.assemble(&patch_offsets, &label_offsets);
    const void* executable = this->buf.append(data);

    // extract the function addresses from the assembled code
    this->jump_return_40 = NULL;
    this->jump_return_38 = NULL;
    this->jump_return_8 = NULL;
    this->jump_return_0 = NULL;
    for (const auto& it : label_offsets) {
      const void* addr = reinterpret_cast<const void*>(
          reinterpret_cast<const char*>(executable) + it.first);
      if (it.second == "jump_return_40") {
        this->jump_return_40 = addr;
      } else if (it.second == "jump_return_38") {
        this->jump_return_38 = addr;
      } else if (it.second == "jump_return_8") {
        this->jump_return_8 = addr;
      } else if (it.second == "jump_return_0") {
        this->jump_return_0 = addr;
      }
    }

    if (this->debug_flags & DebugFlag::ShowAssembledCells) {
      string dasm = AMD64Assembler::disassemble(data.data(), data.size(),
          reinterpret_cast<uint64_t>(executable), &label_offsets);
      fprintf(stderr, "compiled special functions:\n%s\n\n", dasm.c_str());
    }
  }

  // set up the common objects array
  this->add_common_object("%" PRId64, "%" PRId64);
  this->add_common_object("%" PRId64 " ", "%" PRId64 " ");
  this->add_common_object("0 ", "0 ");
  this->add_common_object("jump_return_40", this->jump_return_40);
  this->add_common_object("jump_return_38", this->jump_return_38);
  this->add_common_object("jump_return_8", this->jump_return_8);
  this->add_common_object("jump_return_0", this->jump_return_0);
  this->add_common_object("dispatch_compile_cell",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_compile_cell));
  this->add_common_object("dispatch_get_cell_code",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_get_cell_code));
  this->add_common_object("dispatch_interactive_debug_hook",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_interactive_debug_hook));
  this->add_common_object("dispatch_field_read",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_field_read));
  this->add_common_object("dispatch_field_write",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_field_write));
  this->add_common_object("dispatch_print_state",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_print_state));
  this->add_common_object("dispatch_throw_error",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_throw_error));
  this->add_common_object("fputs", reinterpret_cast<const void*>(&fputs));
  this->add_common_object("getchar", reinterpret_cast<const void*>(&getchar));
  this->add_common_object("printf", reinterpret_cast<const void*>(&printf));
  this->add_common_object("putchar", reinterpret_cast<const void*>(&putchar));
  this->add_common_object("rand", reinterpret_cast<const void*>(&rand));
  this->add_common_object("scanf", reinterpret_cast<const void*>(&scanf));
  this->add_common_object("stdout", reinterpret_cast<const void*>(stdout));
  this->add_common_object("this", this);

  // execution enters the initial cell with the stack misaligned (since the call
  // to that code pushed the return address onto the stack implicitly)
  this->compiled_cells.emplace(piecewise_construct, forward_as_tuple(1, false),
      forward_as_tuple());
}

void BefungeJITCompiler::set_breakpoint(const Position& pos) {
  this->breakpoint_positions.emplace(pos);
}

void BefungeJITCompiler::execute() {
  Position start_pos(1, false);
  CompiledCell& start_cell = this->compiled_cells[start_pos];

  if (!start_cell.code) {
    this->compile_cell(start_pos);
  }

  void (*start)() = reinterpret_cast<void(*)()>(start_cell.code);
  start();
}

void BefungeJITCompiler::check_dimensions(uint8_t required_dimensions,
    const Position& where, int16_t opcode) const {
  if (this->dimensions < required_dimensions) {
    string where_str = where.str();
    throw invalid_argument(string_printf(
        "opcode %c (at %s) only valid in %hhu or more dimensions", opcode,
        where_str.c_str(), required_dimensions));
  }
}

BefungeJITCompiler::CompiledCell::CompiledCell() : code(NULL), code_size(0),
    buffer_capacity(0) { }
BefungeJITCompiler::CompiledCell::CompiledCell(void* code, size_t code_size) :
    code(code), code_size(code_size), buffer_capacity(code_size) { }
BefungeJITCompiler::CompiledCell::CompiledCell(const Position& dependency) :
    code(NULL), code_size(0), buffer_capacity(0),
    address_dependencies({dependency}) { }

void BefungeJITCompiler::compile_opcode(AMD64Assembler& as, const Position& pos,
    int16_t opcode) {

  CompiledCell& cell = this->compiled_cells[pos];

  switch (opcode) {
    case -1:
      throw logic_error(string_printf(
          "attempted to compile boundary cell %zd %zd", pos.x, pos.y));

    case '{': // open a new stack
      // secondary stacks are implemented by pushing r13 and resetting it to
      // create a "new" empty stack

      // before this operation, the stack is:
      // rsp=count e1 e2 ... en f1 f2 ... r13=fn
      // if count >= 0, after this operation, the stacks are:
      // rsp=e1 e2 ... r13=en oldr13 [[oldSOz] oldSOy] oldSOx f1 f2 ... fn
      // if count < 0, after this operation, the stacks are:
      // r13=? rsp=oldr13 [[oldSOz] oldSOy] oldSOx 0 0 ... 0 f1 f2 ... fn

      // TODO: currently this is O(n); can we make it faster somehow?
      // (also with '}')
      as.write_cmp(rsp, r13);
      as.write_jg("stack_empty");

      // get the count and make space for the storage offset on the second stack
      as.write_mov(rcx, MemoryReference(rsp, 0)); // item count to copy
      as.write_cmp(rcx, 0);
      as.write_jge("count_nonnegative");

      // keep track of stack alignment, sigh. we flip the alignment flag here
      // because we'll pop the count below. but in even dimensions, it flips
      // again when we push the storage offset and old r13 value, so only flip
      // for odd dimensions.
      if (this->dimensions & 1) {
        as.write_lea(r10, MemoryReference(rcx, 1));
      } else {
        as.write_mov(r10, rcx);
      }
      as.write_and(r10, 1);

      // if the count is negative, push that many zeroes onto the stack
      as.write_add(rsp, 8); // "pop" the count
      as.write_label("push_zero_again");
      as.write_push(0);
      as.write_inc(rcx);
      as.write_js("push_zero_again");

      // push the storage offset and original r13
      for (uint8_t dimension = 0; dimension < this->dimensions; dimension++) {
        as.write_push(this->storage_offset_reference(dimension));
      }
      as.write_push(r13);
      as.write_lea(r13, MemoryReference(rsp, -8));

      // TODO: figure out why r13 is set incorrectly when } is executed sometime
      // after this
      //as.write_int(3);

      // set the storage offset to the next cell position
      {
        Position next_pos = pos.copy().move_forward();
        as.write_mov(this->storage_offset_reference(0), next_pos.x);
        if (this->dimensions > 1) {
          as.write_mov(this->storage_offset_reference(1), next_pos.y);
          if (this->dimensions > 2) {
            as.write_mov(this->storage_offset_reference(2), next_pos.z);
          }
        }
      }

      // jump to the correct cell based on the alignment change
      as.write_test(r10b, 1);
      as.write_jz("opcode_end_alignment_unchanged");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      // now the common case: the count is nonnegative. in this case, we'll copy
      // some items from the currentstack onto the new stack. really this just
      // means moving them down in memory a few spaces, pushing the storage
      // offset, and setting r13 appropriately for the new stack. the old r13
      // value "replaces" the item count on the stack, so we only need to
      // reserve space for the storage offset.
      as.write_label("count_nonnegative");
      as.write_sub(rsp, 8 * this->dimensions);
      as.write_lea(rcx, MemoryReference(rsp, -8, rcx, 8)); // new r13 value

      // can't copy more items than exist on the stack
      // TODO: implement this. it should push extra zeroes instead of failing
      as.write_cmp(rcx, r13);
      as.write_jle("count_not_excessive");
      this->write_throw_error(as,
          "open-block opcode executed with count greater than stack size");
      as.write_label("count_not_excessive");

      as.write_mov(rdx, rsp); // pointer to item being written
      as.write_label("copy_again");
      as.write_cmp(rdx, rcx);
      as.write_jg("copy_done");
      as.write_mov(rax, MemoryReference(rdx, 8 + 8 * this->dimensions));
      as.write_mov(MemoryReference(rdx, 0), rax);
      as.write_add(rdx, 8);
      as.write_jmp("copy_again");
      as.write_label("copy_done");

      // now rdx points to the element past the end of the new stack. here we
      // should write the old r13 value (to restore the old setack later) and
      // the current storage offset (so it's on the top of the second stack)

      // write the old r13 value between the stacks
      as.write_mov(MemoryReference(rdx, 0), r13);

      // copy the storage offset onto the top of the second stack
      // TODO: this should probably be a loop, lolz. but maybe it's more clear
      // like this
      if (this->dimensions == 1) {
        as.write_mov(rax, this->storage_offset_reference(0));
        as.write_mov(MemoryReference(rdx, 8), rax);
      } else if (this->dimensions == 2) {
        as.write_mov(rax, this->storage_offset_reference(0));
        as.write_mov(MemoryReference(rdx, 16), rax);
        as.write_mov(rax, this->storage_offset_reference(1));
        as.write_mov(MemoryReference(rdx, 8), rax);
      } else { // 3D
        as.write_mov(rax, this->storage_offset_reference(0));
        as.write_mov(MemoryReference(rdx, 24), rax);
        as.write_mov(rax, this->storage_offset_reference(1));
        as.write_mov(MemoryReference(rdx, 16), rax);
        as.write_mov(rax, this->storage_offset_reference(2));
        as.write_mov(MemoryReference(rdx, 8), rax);
      }

      // set the storage offset to the next cell position
      {
        Position next_pos = pos.copy().move_forward();
        as.write_mov(this->storage_offset_reference(0), next_pos.x);
        if (this->dimensions > 1) {
          as.write_mov(this->storage_offset_reference(1), next_pos.y);
          if (this->dimensions > 2) {
            as.write_mov(this->storage_offset_reference(2), next_pos.z);
          }
        }
      }

      // set the end-of-stack pointer appropriately
      as.write_mov(r13, rcx);

      // alignment doesn't change in this case because we popped one item (the
      // count) and pushed one "item" (the previous r13 value)
      as.write_label("opcode_end_alignment_unchanged");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      {
        as.write_label("stack_empty");

        Position next_pos = pos.copy().move_forward();
        for (uint8_t dimension = 0; dimension < this->dimensions; dimension++) {
          as.write_push(this->storage_offset_reference(dimension));
        }
        if (!(this->dimensions & 1)) {
          next_pos.change_alignment();
        }
        as.write_push(r13);
        as.write_lea(r13, MemoryReference(rsp, -8));
        this->write_jump_to_cell(as, pos, next_pos);
      }
      break;

    case '}':
      // first check if the stack stack is empty. if so, reflect
      as.write_lea(r8, this->end_of_last_stack_reference());
      as.write_cmp(r13, r8);
      as.write_jl("second_stack_exists");
      this->write_jump_to_cell(as, pos, pos.copy().turn_around().move_forward());
      as.write_label("second_stack_exists");

      // before this operation, the stacks are:
      // count e1 e2 ... en f1 f2 ... r13=fn oldr13 [[oldSOz] oldSOy] oldSOx g1 g2 ... oldr13=gn
      // after this operation, the stack is:
      // e1 e2 ... en g1 g2 ... r13=gn

      // get the count of items to copy
      as.write_cmp(rsp, r13);
      as.write_jle("stack_one_item");
      as.write_label("stack_empty");
      as.write_xor(r11, r11);
      as.write_jmp("transfer_items");
      as.write_label("stack_one_item");
      as.write_pop(r11);

      as.write_label("transfer_items");

      // restore the old storage offset
      if (this->dimensions == 1) {
        as.write_mov(rax, MemoryReference(r13, 16));
        as.write_mov(this->storage_offset_reference(0), rax);
      } else if (this->dimensions == 2) {
        as.write_mov(rax, MemoryReference(r13, 16));
        as.write_mov(this->storage_offset_reference(1), rax);
        as.write_mov(rax, MemoryReference(r13, 24));
        as.write_mov(this->storage_offset_reference(0), rax);
      } else { // 3D
        as.write_mov(rax, MemoryReference(r13, 16));
        as.write_mov(this->storage_offset_reference(2), rax);
        as.write_mov(rax, MemoryReference(r13, 24));
        as.write_mov(this->storage_offset_reference(1), rax);
        as.write_mov(rax, MemoryReference(r13, 32));
        as.write_mov(this->storage_offset_reference(0), rax);
      }

      as.write_cmp(r11, 0);
      as.write_jge("count_nonnegative");

      as.write_label("count_negative");
      //as.write_int(3);
      as.write_lea(rsp, MemoryReference(r13, 8));
      as.write_pop(r13);
      for (int8_t dimension = this->dimensions - 1; dimension >= 0; dimension--) {
        as.write_pop(this->storage_offset_reference(dimension));
      }

      // pop (-r11) items off the stack, but don't allow it to underflow
      as.write_neg(r11);
      as.write_lea(rsp, MemoryReference(rsp, 0, r11, 8));
      as.write_cmp(rsp, r13);
      as.write_jle("opcode_end");
      as.write_lea(rsp, MemoryReference(r13, 8));
      as.write_jmp("opcode_end");

      as.write_label("count_nonnegative");

      // check if the count is greater than the stack size.
      // TODO: implement this. it should push extra zeroes
      as.write_lea(rcx, MemoryReference(rsp, 0, r11, 8));
      as.write_lea(r9, MemoryReference(r13, 8));
      as.write_cmp(rcx, r9);
      as.write_jle("count_not_excessive");
      this->write_throw_error(as,
          "close-block opcode executed with count greater than stack size");
      as.write_label("count_not_excessive");

      // the common case: the count is nonnegative. in this case, copy n items
      // from the stack being destroyed onto the next stack

      // copy items from the top stack to the second stack.
      // rdx = dest ptr, rcx = src ptr, r11 = count remaining
      as.write_lea(rdx, MemoryReference(r13, (this->dimensions + 2) * 8));

      // we're about to overwrite the old r13 value, so load it now
      as.write_mov(r13, MemoryReference(r13, 8));

      as.write_label("copy_again");
      as.write_dec(r11);
      as.write_js("copy_done");
      as.write_sub(rdx, 8);
      as.write_sub(rcx, 8);
      as.write_mov(rax, MemoryReference(rcx, 0));
      as.write_mov(MemoryReference(rdx, 0), rax);
      as.write_jmp("copy_again");
      as.write_label("copy_done");

      as.write_mov(rsp, rdx);

      as.write_label("opcode_end");
      this->write_jump_to_cell_unknown_alignment(as, pos, pos.copy().move_forward());
      break;

    case 'u':
      // if there's no second stack, reflect
      as.write_lea(r8, this->end_of_last_stack_reference());
      as.write_cmp(r13, r8);
      as.write_jl("second_stack_exists");
      this->write_jump_to_cell(as, pos, pos.copy().turn_around().move_forward());
      as.write_label("second_stack_exists");

      // if the top stack is empty, the count is zero, so do nothing
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      as.write_label("stack_sufficient");

      // the action is different for positive vs. negative counts. if the count
      // is zero, do nothing
      as.write_pop(r11); // item count
      as.write_cmp(r11, 0);
      as.write_jg("transfer_to_top_stack");
      as.write_jl("transfer_from_top_stack");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      as.write_label("transfer_to_top_stack");

      // before this opcode, the stack looked like:
      //   rsp=count f1 f2 ... r13=fn oldr13 e1 e2 ... en g1 g2 ... gn
      // but we already popped the count, so now it looks like:
      //   rsp=f1 f2 ... r13=fn oldr13 e1 e2 ... en g1 g2 ... gn

      // now push e1 ... en in that order:
      //   rsp=en ... e2 e1 f1 f2 ... r13=fn oldr13 e1 e2 ... en rdx=g1 g2 ... gn
      // rcx = source ptr
      // rdx = past-the-end ptr
      // r8 = r13 of second stack
      as.write_lea(rcx, MemoryReference(r13, 0x10));
      as.write_lea(rdx, MemoryReference(r13, 0x10, r11, 8));
      as.write_mov(r8, MemoryReference(r13, 8));
      as.write_label("push_again");
      as.write_cmp(rcx, rdx);
      as.write_jge("push_done");
      as.write_cmp(rcx, r8);
      as.write_jg("second_stack_empty");
      as.write_push(MemoryReference(rcx, 0));
      as.write_jmp("push_next_cell");
      as.write_label("second_stack_empty");
      as.write_push(0);
      as.write_label("push_next_cell");
      as.write_add(rcx, 8);
      as.write_jmp("push_again");
      as.write_label("push_done");

      // move everything from rsp up to SOx (inclusive) up by (count) spaces,
      // overwriting original e1 ... en, but don't underflow the second stack:
      //   rsp=en ... e2 e1 f1 f2 ... r13=fn oldr13 rdx=g1 g2 ... gn
      // rcx = dest item ptr
      // rdx = past-the-end pointer
      // r8 = destination delta
      as.write_cmp(rdx, r8);
      as.write_jle("end_pointer_ok");
      as.write_lea(rdx, MemoryReference(r8, 8));
      as.write_label("end_pointer_ok");
      as.write_lea(r8, MemoryReference(rdx, -0x10));
      as.write_sub(r8, r13);
      as.write_lea(rcx, MemoryReference(rdx, -8));
      as.write_sub(rcx, r8);
      as.write_label("shift_forward_again");
      as.write_mov(rax, MemoryReference(rcx, 0));
      as.write_mov(MemoryReference(rcx, 0, r8), rax);
      as.write_sub(rcx, 8);
      as.write_cmp(rcx, rsp);
      as.write_jge("shift_forward_again");

      // shift the stack top and bottom pointers by the same amount
      as.write_add(rsp, r8);
      as.write_add(r13, r8);
      as.write_jmp("opcode_end");

      as.write_label("transfer_from_top_stack");

      // before this opcode, the stack looked like:
      //   rsp=-count e1 e2 ... en f1 f2 ... r13=fn oldr13 g1 g2 ... gn
      // but we already popped the count, so now it looks like:
      //   rsp=e1 e2 ... en f1 f2 ... r13=fn oldr13 g1 g2 ... gn

      // move everything down to make room for the new items on the second stack
      //   rsp=e1 e2 ... en f1 f2 ... r13=fn oldr13 ?1 ?2 ... ?n g1 g2 ... gn
      // rsp = src item ptr
      // rdx = new rsp (for after the loop is done)
      // rcx = new r13 (for after the loop is done)
      // r8 = past-tne-end pointer (to know when to terminate the loop)
      // we will always shift at least one item (since oldr13 must be present)
      // so we don't have to check before running the first loop iteration
      as.write_lea(rdx, MemoryReference(rsp, 0, r11, 8));
      as.write_lea(rcx, MemoryReference(r13, 0, r11, 8));
      as.write_lea(r8, MemoryReference(r13, 0x10));
      as.write_label("shift_backward_again");
      as.write_pop(rax);
      as.write_mov(MemoryReference(rsp, -8, r11, 8), rax);
      as.write_cmp(rsp, r8);
      as.write_jl("shift_backward_again");
      as.write_mov(rsp, rdx);
      as.write_mov(r13, rcx);

      // now pop those dudes onto the second stack
      //   rsp=f1 f2 ... r13=fn oldr13 en ... e2 e1 g1 g2 ... gn
      as.write_lea(rdx, MemoryReference(r13, 0x10));
      as.write_label("pop_again");
      as.write_cmp(rsp, r13);
      as.write_jg("top_stack_empty");
      as.write_mov(MemoryReference(rdx, 0), 0);
      as.write_jmp("pop_next_cell");
      as.write_label("top_stack_empty");
      as.write_pop(MemoryReference(rdx, 0));
      as.write_label("pop_next_cell");
      as.write_add(rdx, 8);
      as.write_inc(r11);
      as.write_js("pop_again");

      as.write_label("opcode_end");
      this->write_jump_to_cell_unknown_alignment(as, pos, pos.copy().move_forward());
      break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
      if (opcode >= 'a') {
        as.write_push(opcode - 'a' + 10);
      } else {
        as.write_push(opcode - '0');
      }
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;

    case 'w':
      this->check_dimensions(2, pos, 'w');

      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, do nothing (0 == 0)
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      // if there's one item on the stack. turn right if it's positive, left if
      // it's negative
      as.write_label("stack_one_item");
      as.write_pop(rcx);
      as.write_cmp(rcx, 0);
      as.write_jl("stack_one_item_left");
      as.write_jg("stack_one_item_right");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      as.write_label("stack_one_item_left");
      this->write_jump_to_cell(as, pos, pos.copy().turn_left().move_forward().change_alignment());
      as.write_label("stack_one_item_right");
      this->write_jump_to_cell(as, pos, pos.copy().turn_right().move_forward().change_alignment());

      // if there are two or more items on the stack, operate on them
      as.write_label("stack_sufficient");
      as.write_pop(rcx);
      as.write_pop(rax);
      as.write_cmp(rax, rcx);
      as.write_jl("stack_sufficient_left");
      as.write_jg("stack_sufficient_right");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      as.write_label("stack_sufficient_left");
      this->write_jump_to_cell(as, pos, pos.copy().turn_left().move_forward());
      as.write_label("stack_sufficient_right");
      this->write_jump_to_cell(as, pos, pos.copy().turn_right().move_forward());
      break;

    case '`':
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, leave it alone (the result is 0)
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      // if there's one item on the stack:
      //   +: leave it there (0 + x)
      //   -: negate it (0 - x)
      //   *: replace it with zero (0 * x)
      //   /: replace it with zero (0 / x)
      //   %: replace it with zero (0 % x)
      //   `: push 1 if the top is negative, 0 otherwise
      as.write_label("stack_one_item");
      if (opcode == '`') {
        as.write_xor(rdx, rdx);
        as.write_cmp(MemoryReference(rsp, 0), 0);
        as.write_setl(dl);
        as.write_mov(MemoryReference(rsp, 0), rdx);
      } else if (opcode == '-') {
        as.write_neg(MemoryReference(rsp, 0));
      } else if (opcode != '+') {
        as.write_mov(MemoryReference(rsp, 0), 0);
      }
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      // if there are two or more items on the stack, operate on them
      as.write_label("stack_sufficient");
      as.write_pop(rcx);
      if (opcode == '`') {
        as.write_xor(rdx, rdx);
        as.write_cmp(MemoryReference(rsp, 0), rcx);
        as.write_setg(dl);
        as.write_mov(MemoryReference(rsp, 0), rdx);
      } else if (opcode == '+') {
        as.write_add(MemoryReference(rsp, 0), rcx);
      } else if (opcode == '-') {
        as.write_sub(MemoryReference(rsp, 0), rcx);
      } else if (opcode == '*') {
        // imul destination has to be a register
        as.write_imul(rcx, MemoryReference(rsp, 0));
        as.write_mov(MemoryReference(rsp, 0), rcx);
      } else {
        as.write_xor(rdx, rdx);
        as.write_mov(rax, MemoryReference(rsp, 0));
        as.write_test(rcx, rcx);
        as.write_jz("division_by_zero");
        as.write_idiv(rcx);
        as.write_mov(MemoryReference(rsp, 0), (opcode == '%') ? rdx : rax);
        as.write_jmp("division_complete");
        as.write_label("division_by_zero");
        as.write_mov(MemoryReference(rsp, 0), 0);
        as.write_label("division_complete");
      }
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;

    case '!': // logical not
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");
      as.write_push(1);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      as.write_label("stack_sufficient");
      as.write_pop(rax);
      as.write_test(rax, rax);
      as.write_setz(al);
      as.write_movzx8(rax, al);
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      break;

    case 'z': // "go through" (noop)
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      break;
    case '<': // move left
      this->write_jump_to_cell(as, pos, pos.copy().face(-1, 0, 0).move_forward());
      break;
    case '>': // move right
      this->write_jump_to_cell(as, pos, pos.copy().face(1, 0, 0).move_forward());
      break;
    case '^': // move up
      this->check_dimensions(2, pos, '^');
      this->write_jump_to_cell(as, pos, pos.copy().face(0, -1, 0).move_forward());
      break;
    case 'v': // move down
      this->check_dimensions(2, pos, 'v');
      this->write_jump_to_cell(as, pos, pos.copy().face(0, 1, 0).move_forward());
      break;
    case 'h': // move above
      this->check_dimensions(3, pos, 'h');
      this->write_jump_to_cell(as, pos, pos.copy().face(0, 0, -1).move_forward());
      break;
    case 'l': // move below
      this->check_dimensions(3, pos, 'l');
      this->write_jump_to_cell(as, pos, pos.copy().face(0, 0, 1).move_forward());
      break;
    case '[': // turn left
      this->check_dimensions(2, pos, '[');
      this->write_jump_to_cell(as, pos, pos.copy().turn_left().move_forward());
      break;
    case ']': // turn right
      this->check_dimensions(2, pos, ']');
      this->write_jump_to_cell(as, pos, pos.copy().turn_right().move_forward());
      break;
    case 'r': // reverse
      this->write_jump_to_cell(as, pos, pos.copy().turn_around().move_forward());
      break;

    case '?': // move randomly
      this->write_function_call(as, this->common_object_reference("rand"),
          pos.stack_aligned);

      as.write_mov(rcx, "jump_table");
      if (this->dimensions == 1) {
        as.write_and(rax, 1);
        as.write_jmp(MemoryReference(rcx, 0, rax, 8));
        this->write_jump_table(as, "jump_table", pos, {
            pos.copy().face(-1, 0, 0).move_forward(),
            pos.copy().face(1, 0, 0).move_forward()});

      } else if (this->dimensions == 2) {
        as.write_and(rax, 2);
        as.write_jmp(MemoryReference(rcx, 0, rax, 8));
        this->write_jump_table(as, "jump_table", pos, {
            pos.copy().face(-1, 0, 0).move_forward(),
            pos.copy().face(1, 0, 0).move_forward(),
            pos.copy().face(0, -1, 0).move_forward(),
            pos.copy().face(0, 1, 0).move_forward()});

      } else { // 3 dimensions
        as.write_xor(rdx, rdx);
        as.write_mov(rcx, 6); // apparently there's no imm idiv
        as.write_idiv(rcx);
        as.write_mov(rax, rdx);
        as.write_jmp(MemoryReference(rcx, 0, rax, 8));
        this->write_jump_table(as, "jump_table", pos, {
            pos.copy().face(-1, 0, 0).move_forward(),
            pos.copy().face(1, 0, 0).move_forward(),
            pos.copy().face(0, -1, 0).move_forward(),
            pos.copy().face(0, 1, 0).move_forward(),
            pos.copy().face(0, 0, -1).move_forward(),
            pos.copy().face(0, 0, 1).move_forward()});
      }
      break;

    case 'm': // below if zero, above if not
      this->check_dimensions(3, pos, 'm');
    case '|': // down if zero, up if not
      this->check_dimensions(2, pos, '|');
    case '_': { // right if zero, left if not
      as.write_xor(rcx, rcx);

      // if the stack is empty, don't read from it - the value is zero
      as.write_cmp(rsp, r13);
      as.write_jle("stack_nonempty");

      if (opcode == '_') {
        this->write_jump_to_cell(as, pos, pos.copy().face(1, 0, 0).move_forward());
      } else if (opcode == '|') {
        this->write_jump_to_cell(as, pos, pos.copy().face(0, 1, 0).move_forward());
      } else { // 'm'
        this->write_jump_to_cell(as, pos, pos.copy().face(0, 0, 1).move_forward());
      }

      as.write_label("stack_nonempty");
      as.write_pop(rax);
      as.write_test(rax, rax);
      as.write_setnz(rcx);

      as.write_mov(rax, "jump_table");
      as.write_jmp(MemoryReference(rax, 0, rcx, 8));
      Position new_pos = pos.copy().change_alignment();

      if (opcode == '_') {
        this->write_jump_table(as, "jump_table", pos,
            {new_pos.copy().face(1, 0, 0).move_forward(),
             new_pos.copy().face(-1, 0, 0).move_forward()});
      } else if (opcode == '|') {
        this->write_jump_table(as, "jump_table", pos,
            {new_pos.copy().face(0, 1, 0).move_forward(),
             new_pos.copy().face(0, -1, 0).move_forward()});
      } else { // 'm'
        this->write_jump_table(as, "jump_table", pos,
            {new_pos.copy().face(0, 0, 1).move_forward(),
             new_pos.copy().face(0, 0, -1).move_forward()});
      }
      break;
    }

    case '\'': { // read program space immediately following this instruction
      Position target_pos = pos.copy().move_forward().wrap_lahey(this->field);
      as.write_mov(rdi, this->common_object_reference("this"));
      as.write_mov(rsi, target_pos.x);
      as.write_mov(rdx, target_pos.y);
      as.write_mov(rcx, target_pos.z);
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, target_pos.move_forward().change_alignment());
      break;
    }

    case 's': { // write program space immediately following this instruction
      // both cases end up calling a function with these args
      Position target_pos = pos.copy().move_forward().wrap_lahey(this->field);
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_mov(rdx, target_pos.x);
      as.write_mov(rcx, target_pos.y);
      as.write_mov(r8, target_pos.z);

      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      // stack is empty; write a zero
      {
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, target_pos.copy().move_forward());

        as.write_mov(rsi, token);
        as.write_xor(r9, r9);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_0"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_8"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }

      // stack is not empty; write a value from the stack
      as.write_label("stack_sufficient");
      {
        target_pos.change_alignment();
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, target_pos.copy().move_forward());

        as.write_mov(rsi, token);
        as.write_pop(r9);
        if (!pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_0"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_8"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }
      break;
    }

    case '\"': { // push an entire string
      // TODO: this needs to depend on the VALUE of the following cells, not
      // just their addresses (which it currently doesn't depend on either)
      Position char_pos = pos.copy().move_forward();
      int16_t last_value = 0;
      for (;;) {
        int16_t value = this->field.get(char_pos.x, char_pos.y, char_pos.z);
        if (value == '\"') {
          break;
        }

        if ((value != ' ') || (last_value != ' ')) {
          as.write_push(value);
          char_pos.change_alignment();
        }
        char_pos.move_forward();
        last_value = value;
      }

      // char_pos now points to the terminal quote; we should go one beyond
      this->write_jump_to_cell(as, pos, char_pos.move_forward());
      break;
    }

    case ':': // duplicate top of stack
      as.write_xor(rax, rax);
      as.write_cmp(rsp, r13);
      as.write_cmovle(rax, MemoryReference(rsp, 0));
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;

    case '\\': // swap top 2 items on stack
      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, do nothing (the top 2 values are zeroes)
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      // if there's one item on the stack, just push a zero after it
      as.write_label("stack_one_item");
      as.write_push(0);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      // if there are two or more items on the stack, swap them
      as.write_label("stack_sufficient");
      as.write_pop(rax);
      as.write_xchg(rax, MemoryReference(rsp, 0));
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      break;

    case '$': // discard top of stack
      as.write_cmp(rsp, r13);
      as.write_jg("stack_empty");

      as.write_add(rsp, 8);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      as.write_label("stack_empty");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      break;

    case 'n': // clear stack entirely
      as.write_lea(rsp, MemoryReference(r13, 8));
      this->write_jump_to_cell_unknown_alignment(as, pos, pos.copy().move_forward());
      break;

    case '.': { // pop and print as integer followed by space
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      as.write_xor(rax, rax); // number of float args (printf is variadic)
      as.write_mov(rdi, this->common_object_reference("stdout"));
      as.write_mov(rsi, this->common_object_reference("0 "));
      this->write_function_call(as, this->common_object_reference("fputs"),
          pos.stack_aligned);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      as.write_label("stack_sufficient");
      as.write_xor(rax, rax); // number of float args (printf is variadic)
      as.write_mov(rdi, this->common_object_reference("%" PRId64 " "));
      as.write_pop(rsi);
      this->write_function_call(as, this->common_object_reference("printf"),
          !pos.stack_aligned);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;
    }

    case ',': // pop and print as ascii character
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      as.write_xor(rdi, rdi);
      this->write_function_call(as, this->common_object_reference("putchar"),
          pos.stack_aligned);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      as.write_label("stack_sufficient");
      as.write_pop(rdi);
      this->write_function_call(as, this->common_object_reference("putchar"),
          !pos.stack_aligned);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;

    case ' ': // skip this cell
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      break;

    case '#': { // skip this cell and next cell
      Position wrapped_next = pos.copy().move_forward().wrap_lahey(this->field);
      this->write_jump_to_cell(as, pos, wrapped_next.move_forward());
      break;
    }

    case 'j': { // jump forward by n cells
      // this is harder than it sounds because the distance is on the stack, not
      // statically available. to get the resulting cell we have to call into
      // the compiler, sigh
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      as.write_label("stack_sufficient");

      Position start_pos = pos.copy().move_forward().change_alignment();
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_pop(r11);

      as.write_push(start_pos.stack_aligned);
      as.write_push(start_pos.dz);
      as.write_push(start_pos.dy);
      as.write_push(start_pos.dx);

      static auto write_coord = +[](AMD64Assembler& as, int64_t x, int64_t dx) {
        if (dx) {
          as.write_mov(r8, r11);
          if (dx == -1) {
            as.write_neg(r8);
          } else if (dx != 1) {
            // TODO: use immediate form of imul here
            as.write_mov(r9, dx);
            as.write_imul(r8, r9);
          }
          as.write_add(r8, x);
          as.write_push(r8);
        } else {
          as.write_push(x);
        }
      };

      write_coord(as, start_pos.z, start_pos.dz);
      write_coord(as, start_pos.y, start_pos.dy);
      write_coord(as, start_pos.x, start_pos.dx);

      as.write_mov(rsi, rsp);
      this->write_function_call(as,
          this->common_object_reference("dispatch_get_cell_code"),
          !start_pos.stack_aligned);
      as.write_add(rsp, 0x38); // 7 64-bit fields
      as.write_jmp(rax);
      break;
    }

    case 'x': { // set delta
      as.write_cmp(rsp, r13);
      as.write_jg("stack_empty");

      // all cases end up calling a function with this as the first arg
      as.write_mov(rdi, this->common_object_reference("this"));

      if (this->dimensions == 1) {
        as.write_pop(r8);
        as.write_push(!pos.stack_aligned);
        as.write_push(0); // dz
        as.write_push(0); // dy
        as.write_push(r8); // dx
        as.write_push(0); // z
        as.write_push(0); // y
        as.write_add(r8, pos.x);
        as.write_push(r8); // x

        as.write_mov(rsi, rsp);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_38"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_40"));
        }
        as.write_jmp(this->common_object_reference("dispatch_get_cell_code"));

      } else {
        as.write_jl("stack_two_or_more_items");

        as.write_label("stack_one_item");
        as.write_pop(r8);
        as.write_push(!pos.stack_aligned);
        if (this->dimensions == 2) {
          as.write_push(0); // dz
          as.write_push(r8); // dy
        } else { // 3D
          as.write_push(r8); // dz
          as.write_push(0); // dy
        }
        as.write_push(0); // dx
        if (this->dimensions == 2) {
          as.write_push(0); // z
          as.write_add(r8, pos.y);
          as.write_push(r8); // y
        } else { // 3D
          as.write_add(r8, pos.x);
          as.write_push(r8); // z
          as.write_push(pos.y); // y
        }
        as.write_push(pos.x); // x

        as.write_mov(rsi, rsp);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_38"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_40"));
        }
        as.write_jmp(this->common_object_reference("dispatch_get_cell_code"));

        as.write_label("stack_two_or_more_items");
        as.write_pop(r10);
        as.write_pop(r9);
        if (this->dimensions == 2) {
          as.write_push(pos.stack_aligned);
          as.write_push(0); // dz
          as.write_push(r10); // dy
          as.write_push(r9); // dx
          as.write_push(0); // z
          as.write_add(r10, pos.y);
          as.write_push(r10); // y
          as.write_add(r9, pos.x);
          as.write_push(r9); // x

          as.write_mov(rsi, rsp);
          if (!pos.stack_aligned) {
            as.write_push(this->common_object_reference("jump_return_38"));
          } else {
            as.write_sub(rsp, 8);
            as.write_push(this->common_object_reference("jump_return_40"));
          }
          as.write_jmp(this->common_object_reference("dispatch_get_cell_code"));

        } else { // 3D
          as.write_cmp(rsp, r13);
          as.write_jle("stack_three_or_more_items");

          as.write_label("stack_two_items");
          as.write_push(pos.stack_aligned);
          as.write_push(r10); // dz
          as.write_push(r9); // dy
          as.write_push(0); // dx
          as.write_add(r10, pos.z);
          as.write_push(r10); // z
          as.write_add(r9, pos.y);
          as.write_push(r9); // y
          as.write_push(pos.x); // x

          as.write_mov(rsi, rsp);
          if (!pos.stack_aligned) {
            as.write_push(this->common_object_reference("jump_return_38"));
          } else {
            as.write_sub(rsp, 8);
            as.write_push(this->common_object_reference("jump_return_40"));
          }
          as.write_jmp(this->common_object_reference("dispatch_get_cell_code"));

          as.write_label("stack_three_or_more_items");
          as.write_pop(r8);
          as.write_push(pos.stack_aligned);
          as.write_push(r10); // dz
          as.write_push(r9); // dy
          as.write_push(r8); // dx
          as.write_add(r10, pos.z);
          as.write_push(r10); // z
          as.write_add(r9, pos.y);
          as.write_push(r9); // y
          as.write_add(r8, pos.x);
          as.write_push(r8); // x

          as.write_mov(rsi, rsp);
          if (pos.stack_aligned) {
            as.write_push(this->common_object_reference("jump_return_38"));
          } else {
            as.write_sub(rsp, 8);
            as.write_push(this->common_object_reference("jump_return_40"));
          }
          as.write_jmp(this->common_object_reference("dispatch_get_cell_code"));
        }
      }

      // it's an error to execute 'x' with an empty stack - this would set
      // dx = dy = dz = 0, so execution would loop forever on this cell. we
      // raise an error instead.
      as.write_label("stack_empty");
      this->write_throw_error(as, "cannot execute x opcode on empty stack");
      break;
    }

    case ';': { // skip everything until the next ';'
      Position char_pos = pos.copy().move_forward();
      for (;;) {
        int16_t value = this->field.get(char_pos.x, char_pos.y, char_pos.z);
        if (value == ';') {
          break;
        }
        char_pos.move_forward();
      }

      // char_pos now points to the terminal semicolon; we should go one beyond
      this->write_jump_to_cell(as, pos, char_pos.move_forward());
      break;
    }

    case 'k': { // execute the next instruction n times
      Position char_pos = pos.copy().move_forward();
      int16_t value;
      bool in_semicolon = false;
      for (;;) {
        value = this->field.get(char_pos.x, char_pos.y, char_pos.z);
        if (value == ';') {
          in_semicolon = !in_semicolon;
        } else if (!in_semicolon && (value != ' ') && (value != 'Y')) {
          break;
        }
        char_pos.move_forward().wrap_lahey(this->field);
      }

      this->compile_opcode_iterated(as, pos, char_pos, value);
      break;
    }

    case 'p': // write program space
      // all cases end up calling a function with this first arg
      as.write_mov(rdi, this->common_object_reference("this"));

      // TODO: reduce ugly code duplication below
      // TODO: use storage offset in this command and in 'g'

      // since we need 3 values from the stack, there are 4 cases here
      as.write_cmp(rsp, r13);
      as.write_jl("stack_two_or_more_items");
      as.write_je("stack_one_item");

      // stack empty
      as.write_label("stack_empty");
      this->write_load_storage_offset(as, {{rdx, false}, {rcx, false}, {r8, false}});
      as.write_xor(r9, r9); // value
      as.write_jmp("call_same_alignment");

      // stack has 1 item
      as.write_label("stack_one_item");
      as.write_pop(rdx); // x
      this->write_load_storage_offset(as, {{rdx, true}, {rcx, false}, {r8, false}});
      as.write_xor(r9, r9); // value
      as.write_jmp("call_other_alignment");

      // stack has 2 or more items; pop the first 2 and check again. but if this
      // is one-dimensional, we only need two arguments (hooray)
      as.write_label("stack_two_or_more_items");
      if (this->dimensions == 1) {
        as.write_pop(rdx); // x
        this->write_load_storage_offset(as, {{rdx, true}, {rcx, false}, {r8, false}});
        as.write_pop(r9); // value
        as.write_jmp("call_same_alignment");

      } if (this->dimensions == 2) {
        as.write_pop(rcx); // y
        as.write_pop(rdx); // x

        as.write_cmp(rsp, r13);
        as.write_jle("stack_three_or_more_items");

        // stack has no more items after the popped two
        as.write_label("stack_two_items");
        this->write_load_storage_offset(as, {{rdx, true}, {rcx, true}, {r8, false}});
        as.write_xor(r9, r9); // value
        as.write_jmp("call_same_alignment");

        // stack has one item remaining after the popped two
        as.write_label("stack_three_or_more_items");
        this->write_load_storage_offset(as, {{rdx, true}, {rcx, true}, {r8, false}});
        as.write_pop(r9); // value
        as.write_jmp("call_other_alignment");

      } else { // dimensions == 3
        as.write_pop(r8); // z
        as.write_pop(rcx); // y

        as.write_cmp(rsp, r13);
        as.write_jl("stack_four_or_more_items");
        as.write_je("stack_three_items");

        // stack has no more items after the popped two
        as.write_label("stack_two_items");
        this->write_load_storage_offset(as, {{rdx, false}, {rcx, true}, {r8, true}});
        as.write_xor(r9, r9); // value
        as.write_jmp("call_same_alignment");

        // stack has one item remaining after the popped two
        as.write_label("stack_three_items");
        as.write_pop(rdx); // x
        this->write_load_storage_offset(as, {{rdx, true}, {rcx, true}, {r8, true}});
        as.write_xor(r9, r9); // value
        as.write_jmp("call_other_alignment");

        // stack has two or more items remaining after the popped two
        as.write_label("stack_four_or_more_items");
        as.write_pop(rdx); // x
        this->write_load_storage_offset(as, {{rdx, true}, {rcx, true}, {r8, true}});
        as.write_pop(r9); // value
        as.write_jmp("call_same_alignment");
      }

      as.write_label("call_other_alignment");
      {
        Position next_pos = pos.copy().move_forward().change_alignment().wrap_lahey(this->field);
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, next_pos);

        as.write_mov(rsi, token);
        if (!pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_0"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_8"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }

      as.write_label("call_same_alignment");
      {
        Position next_pos = pos.copy().move_forward().wrap_lahey(this->field);
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, next_pos);

        as.write_mov(rsi, token);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("jump_return_0"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("jump_return_8"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }
      break;

    case 'g': // read program space
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_cmp(rsp, r13);
      as.write_je("stack_one_item");
      if (this->dimensions > 1) {
        as.write_jl("stack_two_or_more_items");
      }

      as.write_label("stack_empty");
      this->write_load_storage_offset(as, {{rsi, false}, {rdx, false}, {rcx, false}});
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      as.write_label("stack_one_item");
      if (this->dimensions == 1) {
        as.write_pop(rsi);
        this->write_load_storage_offset(as, {{rsi, true}, {rdx, false}, {rcx, false}});
      } else if (this->dimensions == 2) {
        as.write_pop(rdx);
        this->write_load_storage_offset(as, {{rsi, false}, {rdx, true}, {rcx, false}});
      } else {
        as.write_pop(rcx);
        this->write_load_storage_offset(as, {{rsi, false}, {rdx, false}, {rcx, true}});
      }
      this->write_function_call(as, 
          this->common_object_reference("dispatch_field_read"), !pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward());

      if (this->dimensions == 2) {
        as.write_label("stack_two_or_more_items");
        as.write_pop(rdx); // y
        as.write_pop(rsi); // x
        this->write_load_storage_offset(as, {{rsi, true}, {rdx, true}, {rcx, false}});
        this->write_function_call(as, 
            this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
        as.write_push(rax);
        this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

      } else if (dimensions == 3) {
        as.write_label("stack_two_or_more_items");
        as.write_pop(rcx); // z
        as.write_pop(rdx); // y

        as.write_cmp(rsp, r13);
        as.write_jle("stack_three_or_more_items");

        as.write_label("stack_two_items");
        this->write_load_storage_offset(as, {{rsi, false}, {rdx, true}, {rcx, false}});
        this->write_function_call(as, 
            this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
        as.write_push(rax);
        this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());

        as.write_label("stack_three_or_more_items");
        as.write_pop(rsi); // x
        this->write_load_storage_offset(as, {{rsi, true}, {rdx, true}, {rcx, false}});
        this->write_function_call(as,
            this->common_object_reference("dispatch_field_read"), !pos.stack_aligned);
        as.write_push(rax);
        this->write_jump_to_cell(as, pos, pos.copy().move_forward());
      }
      break;

    case '&': { // push user-supplied number
      as.write_xor(rax, rax); // number of float args (scanf is variadic)
      as.write_mov(rdi, this->common_object_reference("%" PRId64));
      as.write_push(0);
      as.write_mov(rsi, rsp);
      this->write_function_call(as, this->common_object_reference("scanf"),
          !pos.stack_aligned);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;
    }

    case '~': // push user-supplied character
      this->write_function_call(as, this->common_object_reference("getchar"),
          pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(as, pos, pos.copy().move_forward().change_alignment());
      break;

    case '@': // end program
      as.write_mov(r13, MemoryReference(rbp, -0x10));
      as.write_mov(r12, MemoryReference(rbp, -0x08));
      as.write_mov(rsp, rbp);
      as.write_pop(rbp);
      as.write_ret();
      break;

    case 'Y': // stack debug opcode
      if (this->debug_flags & DebugFlag::EnableStackPrintOpcode) {
        as.write_mov(rdi, rsp);
        as.write_lea(rsi, MemoryReference(r13, 8));
        as.write_sub(rsi, rdi);
        as.write_shr(rsi, 3);
        as.write_push(pos.stack_aligned);
        as.write_push(pos.dz);
        as.write_push(pos.dy);
        as.write_push(pos.dx);
        as.write_push(pos.z);
        as.write_push(pos.y);
        as.write_push(pos.x);
        as.write_mov(rdx, rsp);
        as.write_lea(rcx, this->storage_offset_reference(this->dimensions - 1));
        as.write_mov(r8, this->dimensions);
        this->write_function_call(as,
            this->common_object_reference("dispatch_print_state"), !pos.stack_aligned);
        as.write_add(rsp, 7 * 8);
        this->write_jump_to_cell(as, pos, pos.copy().move_forward());
        break;
      }

    default:
      throw invalid_argument(string_printf(
          "can\'t compile character %c at (%zd, %zd)", opcode, pos.x, pos.y));
  }
}

void BefungeJITCompiler::compile_opcode_iterated(AMD64Assembler& as,
    const Position& iterator_pos, const Position& target_pos, int16_t opcode) {
  // iterator_pos and target_pos refer to the position before the count is
  // popped (immediately below)

  as.write_label(string_printf("iterated_subopcode_%c", opcode));

  // get the iteration count
  as.write_cmp(rsp, r13);
  as.write_jg("opcode_end_zero_count_alignment_same");

  as.write_pop(r11);
  as.write_test(r11, r11);
  as.write_jz("opcode_end_zero_count_alignment_changed");

  as.write_mov(r10, 1);

  // if the iteration count is odd, then the resulting jmp afterward may need to
  // change stack alignments, so keep track of that here

  switch (opcode) {
    case -1:
      throw logic_error(string_printf(
          "attempted to compile boundary cell %zd %zd", target_pos.x, target_pos.y));

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
      // the stack alignment will change once per loop iteration. if the
      // iteration count is odd, the resulting alignment will be opposite.
      as.write_xor(r10b, r11b, OperandSize::Byte);
      as.write_and(r10b, 1, OperandSize::Byte);

      as.write_label("iterate_again");
      as.write_dec(r11);
      as.write_js("iterate_done");
      if (opcode >= 'a') {
        as.write_push(opcode - 'a' + 10);
      } else {
        as.write_push(opcode - '0');
      }
      as.write_jmp("iterate_again");
      as.write_label("iterate_done");
      break;

    case '$': // discard n stack items
      as.write_lea(rdx, MemoryReference(rsp, 0, r11, 8));
      as.write_cmp(rdx, r13);
      as.write_jle("stack_sufficient");

      // there aren't enough items on the stack to pop them all - just clear the
      // entire stack
      as.write_lea(rsp, MemoryReference(r13, 8));
      this->write_jump_to_cell(as, iterator_pos,
          iterator_pos.copy().move_forward().set_aligned(true));

      // there are enough items on the stack to pop them all. if we pop an odd
      // number of stack items, track the alignment change
      as.write_label("stack_sufficient");
      as.write_xor(r10b, r11b, OperandSize::Byte);
      as.write_and(r10b, 1, OperandSize::Byte);
      as.write_mov(rsp, rdx);
      break;

    case ',': // pop and print as ascii characters
      // we'll pop n items off the stack, so keep track of alignment
      as.write_xor(r10b, r11b, OperandSize::Byte);
      as.write_and(r10b, 1, OperandSize::Byte);

      // because we're calling another function, we have to expect r10/r11 to
      // get destroyed. so we'll save them in r14 and r15 in this implementation
      as.write_push(r10);
      as.write_push(r14);
      as.write_push(r15);
      as.write_lea(r14, MemoryReference(rsp, 0x18));
      if (iterator_pos.stack_aligned) {
        as.write_sub(rsp, 8);
      }

      // r14 = current stack item ptr; r15 = past-the-last stack item ptr
      as.write_lea(r15, MemoryReference(r14, 0, r11, 8));

      as.write_label("iterate_again");
      as.write_cmp(r14, r15);
      as.write_je("iterate_done");
      as.write_mov(rdi, MemoryReference(r14, 0));
      as.write_call(this->common_object_reference("putchar"));
      as.write_add(r14, 8);
      as.write_jmp("iterate_again");
      as.write_label("iterate_done");

      // r15 is where the stack should be when we're done (the first item we
      // didn't pop). so set that up and restore the reg values
      as.write_mov(r8, r15);
      if (iterator_pos.stack_aligned) {
        as.write_add(rsp, 8);
      }
      as.write_mov(r15, MemoryReference(rsp, 0x00));
      as.write_mov(r14, MemoryReference(rsp, 0x08));
      as.write_mov(r10, MemoryReference(rsp, 0x10));
      as.write_mov(rsp, r8);
      break;

    case '`':
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
      as.write_test(r11, r11);
      as.write_jz("opcode_end");

      // if the stack is empty, leave it alone (the result is 0)
      as.write_cmp(rsp, r13);
      as.write_jg("opcode_end");

      as.write_pop(rcx);

      as.write_label("iterate_again");
      as.write_cmp(rsp, r13);
      as.write_jg("stack_empty_in_loop");

      // the stack isn't empty, so pop a value from it and combine appropriately
      as.write_pop(rax);
      as.write_xor(r10b, 1, OperandSize::Byte); // track the alignment change

      if (opcode == '`') {
        as.write_cmp(rax, rcx);
        as.write_setg(cl);
        as.write_movzx8(rcx, cl);
      } else if (opcode == '+') {
        as.write_add(rcx, rax);
      } else if (opcode == '-') {
        as.write_neg(rcx);
        as.write_add(rcx, rax);
      } else if (opcode == '*') {
        as.write_imul(rcx, rax);
      } else {
        as.write_xor(rdx, rdx);
        as.write_test(rcx, rcx);
        as.write_jz("division_by_zero");

        as.write_idiv(rcx);
        as.write_mov(rcx, (opcode == '%') ? rdx : rax);
        as.write_jmp("division_complete");

        as.write_label("division_by_zero");
        as.write_xor(rcx, rcx);
        as.write_label("division_complete");
      }
      as.write_jmp("iterate_check");

      // the stack is empty. behave as if we had popped zero and terminate the
      // loop early
      as.write_label("stack_empty_in_loop");
      if (opcode == '`') {
        this->write_throw_error(as,
            "unimplemented stack empty condition on iterated ` opcode");
      } else if (opcode == '-') {
        as.write_test(r11, 1);
        as.write_jnz("skip_neg");
        as.write_neg(rcx);
        as.write_label("skip_neg");
      } else if (opcode != '+') {
        as.write_xor(rcx, rcx);
      }
      as.write_push(rcx);
      as.write_jmp("opcode_end");

      as.write_label("iterate_check");
      as.write_dec(r11);
      as.write_jnz("iterate_again");
      break;

    case '!': // logical not
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      // if the iteration count is even, leave the stack alone
      as.write_test(r11, 1);
      as.write_jz("opcode_end");
      as.write_push(1);
      as.write_xor(r10b, 1, OperandSize::Byte); // track the alignment change
      as.write_jmp("opcode_end");

      as.write_label("stack_sufficient");
      as.write_pop(rax);
      as.write_test(rax, rax);
      as.write_setnz(al);
      as.write_mov(r10b, r11b, OperandSize::Byte);
      as.write_and(r10b, 1, OperandSize::Byte);
      as.write_xor(al, r10b, OperandSize::Byte);
      as.write_movzx8(rax, al);
      as.write_push(rax);
      break;

    case '<': // move left
    case '>': // move right
    case '^': // move up
    case 'v': // move down
    case 'h': // move above
    case 'l': { // move below
      as.write_test(r11, r11);
      as.write_jz("opcode_end");
      as.write_test(r10b, r10b, OperandSize::Byte);
      as.write_jnz("other_alignment");

      Position result_pos = target_pos.copy();
      if (opcode == '<') {
        result_pos.face(-1, 0, 0).move_forward();
      } else if (opcode == '>') {
        result_pos.face(1, 0, 0).move_forward();
      } else if (opcode == '^') {
        result_pos.face(0, -1, 0).move_forward();
      } else if (opcode == 'v') {
        result_pos.face(0, 1, 0).move_forward();
      } else if (opcode == 'h') {
        result_pos.face(0, 0, -1).move_forward();
      } else if (opcode == 'l') {
        result_pos.face(0, 0, 1).move_forward();
      }
      this->write_jump_to_cell(as, iterator_pos, result_pos);

      as.write_label("other_alignment");
      result_pos.change_alignment();
      this->write_jump_to_cell(as, iterator_pos, result_pos);
      break;
    }

    case '[': // turn left
      this->check_dimensions(2, target_pos, '[');
      // this is the same as turning right (-n) times
      as.write_neg(r11);

    case ']': { // turn right
      this->check_dimensions(2, target_pos, ']');

      as.write_and(r11, 3);
      as.write_test(r10, 1);
      as.write_jnz("other_alignment");

      as.write_mov(rcx, "same_alignment_jump_table");
      as.write_jmp(MemoryReference(rcx, 0, r11, 8));
      this->write_jump_table(as, "same_alignment_jump_table", iterator_pos, {
          iterator_pos.copy().move_forward(),
          iterator_pos.copy().turn_right().move_forward(),
          iterator_pos.copy().turn_right().turn_right().move_forward(),
          iterator_pos.copy().turn_right().turn_right().turn_right().move_forward()});

      as.write_label("other_alignment");
      const Position iterator_pos_realigned = iterator_pos.copy().change_alignment();
      as.write_mov(rcx, "other_alignment_jump_table");
      as.write_jmp(MemoryReference(rcx, 0, r11, 8));
      this->write_jump_table(as, "other_alignment_jump_table", iterator_pos, {
          iterator_pos_realigned.copy().move_forward(),
          iterator_pos_realigned.copy().turn_right().move_forward(),
          iterator_pos_realigned.copy().turn_right().turn_right().move_forward(),
          iterator_pos_realigned.copy().turn_right().turn_right().turn_right().move_forward()});
      break;
    }

    case '#': { // skip this cell and next n cells
      Position start_pos = iterator_pos.copy().move_forward().change_alignment();
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_push(start_pos.stack_aligned);
      as.write_push(start_pos.dz);
      as.write_push(start_pos.dy);
      as.write_push(start_pos.dx);

      static auto write_coord = +[](AMD64Assembler& as, int64_t x, int64_t dx) {
        if (dx) {
          as.write_mov(r8, r11);
          if (dx == -1) {
            as.write_neg(r8);
          } else if (dx != 1) {
            // TODO: use immediate form of imul here
            as.write_mov(r9, dx);
            as.write_imul(r8, r9);
          }
          as.write_add(r8, x);
          as.write_push(r8);
        } else {
          as.write_push(x);
        }
      };

      write_coord(as, start_pos.z, start_pos.dz);
      write_coord(as, start_pos.y, start_pos.dy);
      write_coord(as, start_pos.x, start_pos.dx);

      as.write_mov(rsi, rsp);
      this->write_function_call(as,
          this->common_object_reference("dispatch_get_cell_code"),
          !start_pos.stack_aligned);
      as.write_add(rsp, 0x38); // 7 64-bit fields
      as.write_jmp(rax);
      break;
    }

    // TODO HERE: convert the rest of this function to iterated opcodes

    default:
      throw invalid_argument(string_printf(
          "can\'t compile iterated character %c at (%zd, %zd)", opcode, target_pos.x, target_pos.y));
  }

  as.write_label("opcode_end");
  as.write_test(r10b, r10b, OperandSize::Byte);
  as.write_jnz("opcode_end_alignment_changed");
  this->write_jump_to_cell(as, iterator_pos, iterator_pos.copy().move_forward());

  as.write_label("opcode_end_alignment_changed");
  this->write_jump_to_cell(as, iterator_pos,
      iterator_pos.copy().move_forward().change_alignment());

  // if the target did not execute (the count was zero), then we move forward
  // from target_pos instead of iterator_pos because the target could not have
  // changed the execution direction
  as.write_label("opcode_end_zero_count_alignment_same");
  this->write_jump_to_cell(as, iterator_pos, target_pos.copy().move_forward());

  as.write_label("opcode_end_zero_count_alignment_changed");
  this->write_jump_to_cell(as, iterator_pos,
      target_pos.copy().move_forward().change_alignment());
}

const void* BefungeJITCompiler::compile_cell(const Position& cell_pos,
    bool reset_cell) {
  // we'll compile the given cell, and any other cells that depend on its
  // address (but only if its address changes)
  set<Position> pending_positions({cell_pos});
  while (!pending_positions.empty()) {
    const Position pos = pending_positions.begin()->copy();
    if (!pos.dx && !pos.dy && !pos.dz && !pos.special_cell_id) {
      throw invalid_argument("cannot compile position: " + pos.str());
    }

    pending_positions.erase(pending_positions.begin());
    CompiledCell& cell = this->compiled_cells[pos];

    int16_t opcode = -1;
    string data;
    unordered_set<size_t> patch_offsets;
    multimap<size_t, std::string> label_offsets;
    if (!reset_cell) {
      AMD64Assembler as;
      as.write_label(pos.label());

      if (pos.special_cell_id == 1) {
        as.write_push(rbp);
        as.write_mov(rbp, rsp);

        as.write_push(r12);
        as.write_mov(r12, reinterpret_cast<int64_t>(this->common_objects.data()));
        as.write_push(r13);

        // set up storage offset
        for (uint8_t x = 0; x < this->dimensions; x++) {
          as.write_push(0);
        }

        as.write_lea(r13, MemoryReference(rsp, -8));

        this->write_jump_to_cell(as, pos, Position(0, 0, 0, 1, 0, 0,
            !(this->dimensions & 1)));

      } else {
        opcode = this->field.get(pos.x, pos.y, pos.z);

        // remove the position token if the cell has one already
        for (auto it = cell.next_position_tokens.begin();
             it != cell.next_position_tokens.end();
             it = cell.next_position_tokens.erase(it)) {
          this->token_to_position.erase(*it);
        }

        this->compile_opcode(as, pos, opcode);
      }

      // at this point the code for the cell is complete; we can assemble it and
      // put it in the buffer appropriately
      data = as.assemble(&patch_offsets, &label_offsets);
    }

    bool recompile_dependencies;
    if (data.empty()) {
      // TODO: this leaks memory in the code buffer when resetting cells that
      // previously had code. fix this.
      cell.code = NULL;
      cell.code_size = 0;
      cell.buffer_capacity = 0;
      recompile_dependencies = true;

    } else if (cell.buffer_capacity < data.size()) {
      cell.code = this->buf.append(data, &patch_offsets);
      cell.code_size = data.size();
      cell.buffer_capacity = cell.code_size;

      // the address changed - need to recompile all the cells that depend
      // on this cell's address. clear the address deps, since they'll be
      // repopulated during recompilation if the new compiled form depends
      // on the new address (it probably does, but w/e - should be correct)
      recompile_dependencies = true;

    } else {
      this->buf.overwrite(cell.code, data, &patch_offsets);
      cell.code_size = data.size();

      // the new code fit in the old code's space, so all the cells that jump to
      // the new code are still correct
      recompile_dependencies = false;
    }

    if (recompile_dependencies) {
      for (const auto& dependency_pos : cell.address_dependencies) {
        if (this->debug_flags & DebugFlag::ShowCompilationEvents) {
          string pos_str = pos.str();
          string dependency_str = dependency_pos.str();
          fprintf(stderr, "%s depends on %s\n", dependency_str.c_str(),
              pos_str.c_str());
        }
        pending_positions.emplace(dependency_pos);
      }
      cell.address_dependencies.clear();
    }

    if (!reset_cell && !cell.code) {
      throw logic_error("cell code address not set after compilation");
    }
    if (reset_cell && cell.code) {
      throw logic_error("cell code address not null after cell was reset");
    }

    if (this->debug_flags & DebugFlag::ShowCompilationEvents) {
      string pos_str = pos.str();
      if (reset_cell) {
        fprintf(stderr, "reset cell %s (opcode = %02hX \'%c\')\n",
            pos_str.c_str(), opcode, opcode);
      } else {
        if (this->debug_flags & DebugFlag::ShowAssembledCells) {
          string dasm = AMD64Assembler::disassemble(cell.code, cell.code_size,
              reinterpret_cast<uint64_t>(cell.code), &label_offsets);
          fprintf(stderr, "compiled cell %s (opcode = %02hX \'%c\'):\n%s\n\n",
              pos_str.c_str(), opcode, opcode, dasm.c_str());
        } else {
          fprintf(stderr, "compiled cell %s (opcode = %02hX \'%c\')\n",
              pos_str.c_str(), opcode, opcode);
        }
      }
    }

    reset_cell = false;
  }

  return this->compiled_cells.at(cell_pos).code;
}

void BefungeJITCompiler::write_function_call(AMD64Assembler& as,
    const MemoryReference& function_ref, bool stack_aligned) {
  if (!stack_aligned) {
    as.write_sub(rsp, 8);
  }
  as.write_call(function_ref);
  if (!stack_aligned) {
    as.write_add(rsp, 8);
  }
}

void BefungeJITCompiler::write_jump_to_cell(AMD64Assembler& as,
    const Position& cell_pos, const Position& next_pos) {
  Position& next_pos_norm = next_pos.copy().wrap_lahey(this->field);
  auto& next_cell = this->compiled_cells[next_pos_norm];

  if (this->debug_flags & DebugFlag::InteractiveDebug) {
    as.write_mov(rdi, this->common_object_reference("this"));
    as.write_mov(rcx, rsp);
    as.write_push(cell_pos.stack_aligned);
    as.write_push(cell_pos.dz);
    as.write_push(cell_pos.dy);
    as.write_push(cell_pos.dx);
    as.write_push(cell_pos.z);
    as.write_push(cell_pos.y);
    as.write_push(cell_pos.x);
    as.write_mov(rsi, rsp);
    as.write_push(next_pos_norm.stack_aligned);
    as.write_push(next_pos_norm.dz);
    as.write_push(next_pos_norm.dy);
    as.write_push(next_pos_norm.dx);
    as.write_push(next_pos_norm.z);
    as.write_push(next_pos_norm.y);
    as.write_push(next_pos_norm.x);
    as.write_mov(rdx, rsp);
    as.write_mov(r8, r13);
    as.write_lea(r9, this->end_of_last_stack_reference());

    if (!next_pos.stack_aligned) {
      as.write_sub(rsp, 8);
    }
    as.write_call(this->common_object_reference("dispatch_interactive_debug_hook"));
    if (!next_pos.stack_aligned) {
      as.write_add(rsp, 15 * 8);
    } else {
      as.write_add(rsp, 14 * 8);
    }
  }

  if (next_cell.code) {
    as.write_jmp_abs(next_cell.code);
  } else {
    // dispatch_compile_cell returns the newly-compiled cell's entry point, so
    // we can just jump to that
    as.write_mov(rdi, this->common_object_reference("this"));
    as.write_push(next_pos_norm.stack_aligned);
    as.write_push(next_pos_norm.dz);
    as.write_push(next_pos_norm.dy);
    as.write_push(next_pos_norm.dx);
    as.write_push(next_pos_norm.z);
    as.write_push(next_pos_norm.y);
    as.write_push(next_pos_norm.x);
    as.write_mov(rsi, rsp);
    if (next_pos_norm.stack_aligned) {
      as.write_sub(rsp, 8);
      as.write_push(this->common_object_reference("jump_return_40"));
    } else {
      as.write_push(this->common_object_reference("jump_return_38"));
    }
    as.write_jmp(this->common_object_reference("dispatch_compile_cell"));
  }

  next_cell.address_dependencies.emplace(cell_pos);
}

void BefungeJITCompiler::write_jump_to_cell_unknown_alignment(
    AMD64Assembler& as, const Position& cell_pos, const Position& next_pos) {
  as.write_test(rsp, 8);
  as.write_jz("stack_aligned");
  this->write_jump_to_cell(as, cell_pos, next_pos.copy().set_aligned(false));
  as.write_label("stack_aligned");
  this->write_jump_to_cell(as, cell_pos, next_pos.copy().set_aligned(true));
}

void BefungeJITCompiler::write_jump_table(AMD64Assembler& as,
    const string& label_name, const Position& pos,
    const vector<Position>& positions) {
  vector<Position> normal_positions;
  for (const auto& next_pos : positions) {
    normal_positions.emplace_back(next_pos.copy().wrap_lahey(this->field));
  }

  vector<int64_t> jump_table_contents;
  for (Position next_pos : normal_positions) {
    CompiledCell& next_cell = this->compiled_cells[next_pos];
    if (next_cell.code) {
      jump_table_contents.emplace_back(reinterpret_cast<int64_t>(
          next_cell.code));
      next_cell.address_dependencies.emplace(pos);
    } else {
      as.write_label(label_name + "_" + next_pos.label());
      this->write_jump_to_cell(as, pos, next_pos);
      jump_table_contents.emplace_back(0);
    }
  }

  as.write_label(label_name);
  for (size_t x = 0; x < normal_positions.size(); x++) {
    if (!jump_table_contents[x]) {
      as.write_label_address(label_name + "_" + normal_positions[x].label());
    } else {
      as.write_raw(&jump_table_contents[x], 8);
    }
  }
}

void BefungeJITCompiler::write_load_storage_offset(AMD64Assembler& as,
    const vector<pair<MemoryReference, bool>>& regs) {
  for (uint8_t dimension = 0; dimension < 3; dimension++) {
    const auto& reg_flag = regs[dimension];
    if (dimension < this->dimensions) {
      if (reg_flag.second) {
        as.write_add(reg_flag.first, this->storage_offset_reference(dimension));
      } else {
        as.write_mov(reg_flag.first, this->storage_offset_reference(dimension));
      }
    } else {
      as.write_xor(reg_flag.first, reg_flag.first);
    }
  }
}

void BefungeJITCompiler::write_throw_error(AMD64Assembler& as,
    const char* message) {
  as.write_mov(rdi, reinterpret_cast<int64_t>(message));
  as.write_and(rsp, -0x10);
  as.write_call(this->common_object_reference("dispatch_throw_error"));
}

void BefungeJITCompiler::add_common_object(const string& name, const void* o) {
  auto emplace_ret = this->common_object_index.emplace(name, this->common_objects.size());
  if (emplace_ret.second) {
    this->common_objects.emplace_back(o);
  }
}

MemoryReference BefungeJITCompiler::common_object_reference(const string& name) {
  return MemoryReference(r12, this->common_object_index.at(name) * 8);
}

MemoryReference BefungeJITCompiler::storage_offset_reference(uint8_t dimension) {
  if ((dimension < 0) || (dimension >= this->dimensions)) {
    throw invalid_argument("dimension out of range");
  }
  return MemoryReference(rbp, -0x10 - (8 * dimension));
}

MemoryReference BefungeJITCompiler::end_of_last_stack_reference() {
  return MemoryReference(rbp, -8 * (3 + this->dimensions));
}

const void* BefungeJITCompiler::dispatch_compile_cell(BefungeJITCompiler* c,
    const Position* pos) {
  const void* ret = c->compile_cell(*pos);
  if (c->debug_flags & DebugFlag::ShowCompilationEvents) {
    string pos_str = pos->str();
    fprintf(stderr, "returning control to compiled code at %016" PRIX64 " %s\n",
        reinterpret_cast<uint64_t>(ret), pos_str.c_str());
  }

  return ret;
}

const void* BefungeJITCompiler::dispatch_get_cell_code(BefungeJITCompiler* c,
    const Position* pos) {
  Position normalized_pos = pos->copy().wrap_lahey(c->field);
  try {
    const void* code = c->compiled_cells.at(normalized_pos).code;
    if (code) {
      return code;
    }
  } catch (const out_of_range&) { }

  return c->compile_cell(normalized_pos);
}

int64_t BefungeJITCompiler::dispatch_field_read(BefungeJITCompiler* c,
    int64_t x, int64_t y, int64_t z) {
  return c->field.get(x, y, z);
}

const void* BefungeJITCompiler::dispatch_field_write(BefungeJITCompiler* c,
    int64_t return_position_token, int64_t x, int64_t y, int64_t z,
    int64_t value) {
  c->field.set(x, y, z, value);

  // reset all the cells that this could affect
  // TODO: we should check for remote opcodes, like ", ;, or k here. one
  // solution could just be to reset the entire row and column, but that seems
  // too heavy. ideally we would have a notion of value dependencies as well as
  // address dependencies, and would use that here
  int64_t min_dx = -0x8000000000000000;
  Position pos(x, y, z, min_dx, min_dx, min_dx, false);
  for (auto it = c->compiled_cells.lower_bound(pos);
       it != c->compiled_cells.end(); it++) {
    if ((it->first.x != pos.x) || (it->first.y != pos.y) || (it->first.z != pos.z)) {
      break;
    }
    c->compile_cell(it->first, true);
  }

  const auto& return_position = c->token_to_position.at(return_position_token);
  auto& return_cell = c->compiled_cells[return_position];
  const void* ret = return_cell.code ? return_cell.code : c->compile_cell(return_position);
  if (c->debug_flags & DebugFlag::ShowCompilationEvents) {
    fprintf(stderr, "returning control to compiled code at %016" PRIX64 "\n",
        reinterpret_cast<uint64_t>(ret));
  }
  return ret;
}

void BefungeJITCompiler::dispatch_print_state(const int64_t* stack_top,
    size_t count, const Position* pos, int64_t* storage_offset, int64_t dimensions) {
  string storage_offset_str;
  if (dimensions == 1) {
    storage_offset_str = string_printf("(%" PRId64 ",)", *storage_offset);
  } else if (dimensions == 2) {
    storage_offset_str = string_printf("(%" PRId64 ", %" PRId64 ")",
        storage_offset[1], storage_offset[0]);
  } else if (dimensions == 3) {
    storage_offset_str = string_printf("(%" PRId64 ", %" PRId64 ", %" PRId64 ")",
        storage_offset[2], storage_offset[1], storage_offset[0]);
  } else {
    storage_offset_str = "<<invalid-dimension>>";
  }

  string pos_str = pos->str();
  fprintf(stderr, "[stack debug] at position %s with storage offset %s; stack at %p has %zu items; item 0 at top\n",
      pos_str.c_str(), storage_offset_str.c_str(), stack_top, count);
  for (size_t x = 0; x < count; x++) {
    int64_t item = stack_top[x];
    if (item >= 0x20 && item <= 0x7F) {
      fprintf(stderr, "item %zu: %" PRId64 " (0x%" PRIX64 ") (%c)\n", x, item,
          item, static_cast<char>(item));
    } else {
      fprintf(stderr, "item %zu: %" PRId64 " (0x%" PRIX64 ")\n", x, item, item);
    }
  }
}

void BefungeJITCompiler::interactive_debug_hook(const Position& current_pos,
    const Position& next_pos, int64_t stack_top, int64_t r13,
    int64_t stack_end) {
  if (!(this->debug_flags & DebugFlag::SingleStep)) {
    for (const auto& pos : this->breakpoint_positions) {
      if (pos.x == next_pos.x && pos.y == next_pos.y && pos.z == next_pos.z) {
        this->debug_flags |= DebugFlag::SingleStep;
        break;
      }
    }
  }

  if (this->debug_flags & DebugFlag::SingleStep) {
    string next_pos_str = next_pos.str();
    string current_pos_str = current_pos.str();
    fprintf(stderr, "single step: at = \'%c\' %s, to = '%c' %s\n",
        this->field.get(current_pos.x, current_pos.y, current_pos.z),
        current_pos_str.c_str(),
        this->field.get(next_pos.x, next_pos.y, next_pos.z),
        next_pos_str.c_str());

    for (size_t stack_index = 0; stack_top < stack_end; stack_index++, stack_top += 8) {
      for (size_t item_index = 0; stack_top <= r13; item_index++, stack_top += 8) {
        int64_t item = *reinterpret_cast<int64_t*>(stack_top);
        fprintf(stderr, "[stack %zu : %zu] %" PRId64 " (0x%" PRIX64 ")\n",
            stack_index, item_index, item, item);
      }
      r13 = *reinterpret_cast<int64_t*>(stack_top);
    }
  }
}

void BefungeJITCompiler::dispatch_interactive_debug_hook(BefungeJITCompiler* c,
    const Position* current_pos, const Position* next_pos, int64_t stack_top,
    int64_t r13, int64_t stack_end) {
  c->interactive_debug_hook(*current_pos, *next_pos, stack_top, r13, stack_end);
}

void BefungeJITCompiler::dispatch_throw_error(const char* message) {
  throw runtime_error(message);
}
