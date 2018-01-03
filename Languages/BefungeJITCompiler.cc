#include "BefungeJITCompiler.hh"

using namespace std;



BefungeJITCompiler::BefungeJITCompiler(const string& filename,
    uint64_t debug_flags) : debug_flags(debug_flags),
    field(Field::load(filename)), next_token(1) {

  // initialiy, all cells are just calls to the compiler. but watch out: these
  // compiler calls might overwrite the cell that called them, so they can't
  // call the compiler normally - instead, they return to this fragment that
  // makes them "return" to the destination cell
  {
    AMD64Assembler as;
    as.write_label("misaligned_ret");
    as.write_add(rsp, 8);
    as.write_label("aligned_ret");
    as.write_jmp(rax);

    unordered_set<size_t> patch_offsets;
    multimap<size_t, string> label_offsets;
    string data = as.assemble(patch_offsets, &label_offsets);
    const void* executable = this->buf.append(data);

    // extract the function addresses from the assembled code
    this->dispatch_compile_cell_ret_misaligned = NULL;
    this->dispatch_compile_cell_ret_aligned = NULL;
    for (const auto& it : label_offsets) {
      const void* addr = reinterpret_cast<const void*>(
          reinterpret_cast<const char*>(executable) + it.first);
      if (it.second == "misaligned_ret") {
        this->dispatch_compile_cell_ret_misaligned = addr;
      } else if (it.second == "aligned_ret") {
        this->dispatch_compile_cell_ret_aligned = addr;
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
  this->add_common_object("dispatch_compile_cell",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_compile_cell));
  this->add_common_object("dispatch_get_cell_code",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_get_cell_code));
  this->add_common_object("dispatch_compile_cell_ret_aligned",
      reinterpret_cast<const void*>(this->dispatch_compile_cell_ret_aligned));
  this->add_common_object("dispatch_compile_cell_ret_misaligned",
      reinterpret_cast<const void*>(this->dispatch_compile_cell_ret_misaligned));
  this->add_common_object("dispatch_field_read",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_field_read));
  this->add_common_object("dispatch_field_write",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_field_write));
  this->add_common_object("dispatch_print_stack_contents",
      reinterpret_cast<const void*>(&BefungeJITCompiler::dispatch_print_stack_contents));
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

void BefungeJITCompiler::execute() {
  Position start_pos(1, false);
  CompiledCell& start_cell = this->compiled_cells[start_pos];

  if (!start_cell.code) {
    this->compile_cell(start_pos);
  }

  void (*start)() = reinterpret_cast<void(*)()>(start_cell.code);
  start();
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
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;

    case 'w':
      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, do nothing (0 == 0)
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      // if there's one item on the stack. turn right if it's positive, left if
      // it's negative
      as.write_label("stack_one_item");
      as.write_pop(rcx);
      as.write_cmp(rcx, 0);
      as.write_jl("stack_one_item_left");
      as.write_jg("stack_one_item_right");
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      as.write_label("stack_one_item_left");
      this->write_jump_to_cell(pos, as, pos.copy().turn_left().move_forward().change_alignment());
      as.write_label("stack_one_item_right");
      this->write_jump_to_cell(pos, as, pos.copy().turn_right().move_forward().change_alignment());

      // if there are two or more items on the stack, operate on them
      as.write_label("stack_sufficient");
      as.write_pop(rcx);
      as.write_pop(rax);
      as.write_cmp(rax, rcx);
      as.write_jl("stack_sufficient_left");
      as.write_jg("stack_sufficient_right");
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      as.write_label("stack_sufficient_left");
      this->write_jump_to_cell(pos, as, pos.copy().turn_left().move_forward());
      as.write_label("stack_sufficient_right");
      this->write_jump_to_cell(pos, as, pos.copy().turn_right().move_forward());
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
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

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
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

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
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;

    case '!': // logical not
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");
      as.write_push(1);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());

      as.write_label("stack_sufficient");
      as.write_pop(rax);
      as.write_test(rax, rax);
      as.write_setz(al);
      as.write_movzx8(rax, al);
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      break;

    case 'z': // "go through" (noop)
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      break;
    case '<': // move left
      this->write_jump_to_cell(pos, as, pos.copy().face_and_move(Direction::Left));
      break;
    case '>': // move right
      this->write_jump_to_cell(pos, as, pos.copy().face_and_move(Direction::Right));
      break;
    case '^': // move up
      this->write_jump_to_cell(pos, as, pos.copy().face_and_move(Direction::Up));
      break;
    case 'v': // move down
      this->write_jump_to_cell(pos, as, pos.copy().face_and_move(Direction::Down));
      break;
    case '[': // turn left
      this->write_jump_to_cell(pos, as, pos.copy().turn_left().move_forward());
      break;
    case ']': // turn right
      this->write_jump_to_cell(pos, as, pos.copy().turn_right().move_forward());
      break;
    case 'r': // reverse
      this->write_jump_to_cell(pos, as, pos.copy().turn_around().move_forward());
      break;

    case '?': // move randomly
      this->write_function_call(as, this->common_object_reference("rand"),
          pos.stack_aligned);
      as.write_and(rax, 3);
      as.write_mov(rcx, "jump_table");
      as.write_jmp(MemoryReference(rcx, 0, rax, 8));
      this->write_direction_jump_table(as, "jump_table", pos, all_directions);
      break;

    case '_': // right if zero, left if not
    case '|': { // down if zero, up if not
      static vector<Direction> vertical({Direction::Down, Direction::Up});
      static vector<Direction> horizontal({Direction::Right, Direction::Left});

      as.write_xor(rcx, rcx);

      // if the stack is empty, don't read from it - the value is zero
      as.write_cmp(rsp, r13);
      as.write_jle("stack_nonempty");

      Direction empty_dir = ((opcode == '|') ? vertical : horizontal)[0];
      this->write_jump_to_cell(pos, as, pos.copy().face_and_move(empty_dir));

      as.write_label("stack_nonempty");
      as.write_pop(rax);
      as.write_test(rax, rax);
      as.write_setnz(rcx);

      as.write_mov(rax, "jump_table");
      as.write_jmp(MemoryReference(rax, 0, rcx, 8));
      Position new_pos = pos.copy().change_alignment();
      this->write_direction_jump_table(as, "jump_table", new_pos,
          (opcode == '|') ? vertical : horizontal);
      break;
    }

    case '\'': { // read program space immediately following this instruction
      Position target_pos = pos.copy().move_forward().wrap_to_field(this->field);
      as.write_mov(rdi, this->common_object_reference("this"));
      as.write_mov(rsi, target_pos.x);
      as.write_mov(rdx, target_pos.y);
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, target_pos.move_forward().change_alignment());
      break;
    }

    case 's': { // write program space immediately following this instruction
      // both cases end up calling a function with these args
      Position target_pos = pos.copy().move_forward().wrap_to_field(this->field);
      as.write_mov(rdi, this->common_object_reference("this"));
      as.write_mov(rdx, target_pos.x);
      as.write_mov(rcx, target_pos.y);

      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      // stack is empty; write a zero
      {
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, target_pos.copy().move_forward());

        as.write_mov(rsi, token);
        as.write_xor(r8, r8);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_aligned"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_misaligned"));
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
        as.write_pop(r8);
        if (!pos.stack_aligned) {
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_aligned"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_misaligned"));
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
        int16_t value = this->field.get(char_pos.x, char_pos.y);
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
      this->write_jump_to_cell(pos, as, char_pos.move_forward());
      break;
    }

    case ':': // duplicate top of stack
      as.write_xor(rax, rax);
      as.write_cmp(rsp, r13);
      as.write_cmovle(rax, MemoryReference(rsp, 0));
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;

    case '\\': // swap top 2 items on stack
      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, do nothing (the top 2 values are zeroes)
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      // if there's one item on the stack, just push a zero after it
      as.write_label("stack_one_item");
      as.write_push(0);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());

      // if there are two or more items on the stack, swap them
      as.write_label("stack_sufficient");
      as.write_pop(rax);
      as.write_xchg(rax, MemoryReference(rsp, 0));
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      break;

    case '$': // discard top of stack
      as.write_cmp(rsp, r13);
      as.write_jg("stack_empty");

      as.write_add(rsp, 8);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());

      as.write_label("stack_empty");
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      break;

    case 'n': // clear stack entirely
      as.write_lea(rsp, MemoryReference(r13, 8));
      this->write_jump_to_cell(pos, as,
          pos.copy().move_forward().set_aligned(true));
      break;

    case '.': { // pop and print as integer followed by space
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      as.write_xor(rax, rax); // number of float args (printf is variadic)
      as.write_mov(rdi, this->common_object_reference("stdout"));
      as.write_mov(rsi, this->common_object_reference("0 "));
      this->write_function_call(as, this->common_object_reference("fputs"),
          pos.stack_aligned);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      as.write_label("stack_sufficient");
      as.write_xor(rax, rax); // number of float args (printf is variadic)
      as.write_mov(rdi, this->common_object_reference("%" PRId64 " "));
      as.write_pop(rsi);
      this->write_function_call(as, this->common_object_reference("printf"),
          !pos.stack_aligned);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;
    }

    case ',': // pop and print as ascii character
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      as.write_xor(rdi, rdi);
      this->write_function_call(as, this->common_object_reference("putchar"),
          !pos.stack_aligned);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      as.write_label("stack_sufficient");
      as.write_pop(rdi);
      this->write_function_call(as, this->common_object_reference("putchar"),
          !pos.stack_aligned);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;

    case ' ': // skip this cell
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());
      break;

    case '#': // skip this cell and next cell
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().move_forward());
      break;

    case 'j': { // jump forward by n cells
      // this is harder than it sounds because the distance is on the stack, not
      // statically available. to get the resulting cell we have to call into
      // the compiler, sigh
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      as.write_label("stack_sufficient");

      Position start_pos = pos.copy().move_forward().change_alignment();
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_mov(rsi, start_pos.x);
      if (start_pos.dir == Direction::Left) {
        as.write_pop(r11);
        as.write_sub(rsi, r11);
      } else if (start_pos.dir == Direction::Right) {
        as.write_pop(r11);
        as.write_add(rsi, r11);
      }

      as.write_mov(rdx, start_pos.y);
      if (start_pos.dir == Direction::Up) {
        as.write_pop(r11);
        as.write_sub(rdx, r11);
      } else if (start_pos.dir == Direction::Down) {
        as.write_pop(r11);
        as.write_add(rdx, r11);
      }

      as.write_mov(rcx, start_pos.dir);
      as.write_mov(r8, start_pos.stack_aligned);

      this->write_function_call(as,
          this->common_object_reference("dispatch_get_cell_code"),
          start_pos.stack_aligned);
      as.write_jmp(rax);
      break;
    }

    case ';': { // skip everything until the next ';'
      Position char_pos = pos.copy().move_forward();
      for (;;) {
        int16_t value = this->field.get(char_pos.x, char_pos.y);
        if (value == ';') {
          break;
        }
        char_pos.move_forward();
      }

      // char_pos now points to the terminal semicolon; we should go one beyond
      this->write_jump_to_cell(pos, as, char_pos.move_forward());
      break;
    }

    case 'k': { // execute the next instruction n times
      Position char_pos = pos.copy().move_forward();
      int16_t value;
      bool in_semicolon = false;
      for (;;) {
        value = this->field.get(char_pos.x, char_pos.y);
        if (value == ';') {
          in_semicolon = !in_semicolon;
        } else if (!in_semicolon && (value != ' ')) {
          break;
        }
        char_pos.move_forward();
      }
      char_pos.wrap_to_field(this->field);

      this->compile_opcode_iterated(as, pos, char_pos, value);
      break;
    }

    case 'p': // write program space
      // all cases end up calling a function with this first arg
      as.write_mov(rdi, this->common_object_reference("this"));

      // since we need 3 values from the stack, there are 4 cases here
      as.write_cmp(rsp, r13);
      as.write_jl("stack_maybe_sufficient");
      as.write_je("stack_one_item");

      // stack empty
      as.write_xor(rdx, rdx);
      as.write_xor(rcx, rcx);
      as.write_xor(r8, r8);
      as.write_jmp("call_same_alignment");

      // stack has 1 item
      as.write_label("stack_one_item");
      as.write_xor(rdx, rdx);
      as.write_pop(rcx);
      as.write_xor(r8, r8);
      as.write_jmp("call_other_alignment");

      // stack has 2 or more items; pop the first 2 and check again
      as.write_label("stack_maybe_sufficient");
      as.write_pop(rcx);
      as.write_pop(rdx);
      as.write_cmp(rsp, r13);
      as.write_jle("stack_sufficient");

      // stack has no more items after the popped two
      as.write_xor(r8, r8);
      as.write_jmp("call_same_alignment");

      // stack has one or more items remaining after the popped two
      as.write_label("stack_sufficient");
      as.write_pop(r8);

      as.write_label("call_other_alignment");
      {
        Position next_pos = pos.copy().move_forward().change_alignment().wrap_to_field(this->field);
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, next_pos);

        as.write_mov(rsi, token);
        if (!pos.stack_aligned) {
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_aligned"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_misaligned"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }

      as.write_label("call_same_alignment");
      {
        Position next_pos = pos.copy().move_forward().wrap_to_field(this->field);
        int64_t token = this->next_token++;
        cell.next_position_tokens.emplace(token);
        this->token_to_position.emplace(token, next_pos);

        as.write_mov(rsi, token);
        if (pos.stack_aligned) {
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_aligned"));
        } else {
          as.write_sub(rsp, 8);
          as.write_push(this->common_object_reference("dispatch_compile_cell_ret_misaligned"));
        }
        as.write_jmp(this->common_object_reference("dispatch_field_write"));
      }
      break;

    case 'g': // read program space
      as.write_mov(rdi, this->common_object_reference("this"));

      as.write_cmp(rsp, r13);
      as.write_jl("stack_sufficient");
      as.write_je("stack_one_item");

      // if the stack is empty, read (0, 0) and push it
      as.write_xor(rsi, rsi);
      as.write_xor(rdx, rdx);
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());

      // if there's one item on the stack, we'll replace it
      as.write_label("stack_one_item");
      as.write_xor(rsi, rsi);
      as.write_mov(rdx, MemoryReference(rsp, 0));
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), pos.stack_aligned);
      as.write_mov(MemoryReference(rsp, 0), rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward());

      // if there are two or more items on the stack, use both
      as.write_label("stack_sufficient");
      as.write_pop(rdx);
      as.write_mov(rsi, MemoryReference(rsp, 0));
      this->write_function_call(as,
          this->common_object_reference("dispatch_field_read"), !pos.stack_aligned);
      as.write_mov(MemoryReference(rsp, 0), rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;

    case '&': { // push user-supplied number
      as.write_xor(rax, rax); // number of float args (scanf is variadic)
      as.write_mov(rdi, this->common_object_reference("%" PRId64));
      as.write_push(0);
      as.write_mov(rsi, rsp);
      this->write_function_call(as, this->common_object_reference("scanf"),
          !pos.stack_aligned);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
      break;
    }

    case '~': // push user-supplied character
      this->write_function_call(as, this->common_object_reference("getchar"),
          pos.stack_aligned);
      as.write_push(rax);
      this->write_jump_to_cell(pos, as, pos.copy().move_forward().change_alignment());
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
        this->write_function_call(as, this->common_object_reference("dispatch_print_stack_contents"), pos.stack_aligned);
        this->write_jump_to_cell(pos, as, pos.copy().move_forward());
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
      this->write_jump_to_cell(iterator_pos, as,
          iterator_pos.copy().move_forward().set_aligned(true));

      // there are enough items on the stack to pop them all. if we pop an odd
      // number of stack items, track the alignment change
      as.write_label("stack_sufficient");
      as.write_xor(r10b, r11b, OperandSize::Byte);
      as.write_and(r10b, 1, OperandSize::Byte);
      as.write_mov(rsp, rdx);
      break;

    case ',': // pop and print as ascii characters
      // because we're calling another function, we have to expect r0 and r11 to
      // get destroyed. so we'll save them in r14 and r15 in this implementation
      as.write_push(r10);
      as.write_push(r14);
      as.write_push(r15);
      if (iterator_pos.stack_aligned) {
        as.write_sub(rsp, 8);
      }

      // r14 = current stack item ptr; r15 = past-the-last stack item ptr
      as.write_lea(r14, MemoryReference(rsp, 0x10));
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
        as.write_mov(rdi, reinterpret_cast<int64_t>(
            "unimplemented stack empty condition on iterated ` opcode"));
        this->write_function_call(as, this->common_object_reference("dispatch_throw_error"),
            false);
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
    case 'v': { // move down
      as.write_test(r11, r11);
      as.write_jz("opcode_end");
      as.write_test(r10b, r10b, OperandSize::Byte);
      as.write_jnz("other_alignment");

      Position result_pos = target_pos.copy();
      if (opcode == '<') {
        result_pos.face_and_move(Direction::Left);
      } else if (opcode == '>') {
        result_pos.face_and_move(Direction::Right);
      } else if (opcode == '^') {
        result_pos.face_and_move(Direction::Up);
      } else if (opcode == 'v') {
        result_pos.face_and_move(Direction::Down);
      }
      this->write_jump_to_cell(iterator_pos, as, result_pos);

      as.write_label("other_alignment");
      result_pos.change_alignment();
      this->write_jump_to_cell(iterator_pos, as, result_pos);
      break;
    }

    case '[': // turn left
      as.write_neg(r11);
    case ']': { // turn right
      as.write_add(r11, static_cast<int64_t>(iterator_pos.dir));
      as.write_and(r11, 3);
      as.write_test(r10, 1);
      as.write_jnz("other_alignment");

      as.write_mov(rcx, "same_alignment_jump_table");
      as.write_jmp(MemoryReference(rcx, 0, r11, 8));
      this->write_direction_jump_table(as, "same_alignment_jump_table",
          iterator_pos, all_directions);

      as.write_label("other_alignment");
      Position iterator_pos_realigned = iterator_pos.copy().change_alignment();
      as.write_mov(rcx, "other_alignment_jump_table");
      as.write_jmp(MemoryReference(rcx, 0, r11, 8));
      this->write_direction_jump_table(as, "other_alignment_jump_table",
          iterator_pos_realigned, all_directions);
      break;
    }

    case '#': { // skip this cell and next n cells
      as.write_mov(rdi, this->common_object_reference("this"));

      if (iterator_pos.dir == Direction::Left) {
        as.write_neg(r11);
        as.write_lea(rsi, MemoryReference(r11, iterator_pos.x - 1));
      } else if (iterator_pos.dir == Direction::Right) {
        as.write_lea(rsi, MemoryReference(r11, iterator_pos.x + 1));
      } else {
        as.write_mov(rsi, iterator_pos.x);
      }

      if (iterator_pos.dir == Direction::Up) {
        as.write_neg(r11);
        as.write_lea(rdx, MemoryReference(r11, iterator_pos.y - 1));
      } else if (iterator_pos.dir == Direction::Down) {
        as.write_lea(rdx, MemoryReference(r11, iterator_pos.y + 1));
      } else {
        as.write_mov(rdx, iterator_pos.y);
      }

      as.write_mov(rcx, iterator_pos.dir);
      as.write_mov(r8, iterator_pos.stack_aligned);
      as.write_xor(r8b, r10b); // apply alignment change from count pop

      as.write_test(r10, 1);
      if (iterator_pos.stack_aligned) {
        as.write_jnz("misaligned");
      } else {
        as.write_jz("misaligned");
      }

      MemoryReference function_ref = this->common_object_reference(
          "dispatch_get_cell_code");

      this->write_function_call(as, function_ref, true);
      as.write_jmp("got_cell_code");

      as.write_label("misaligned");
      this->write_function_call(as, function_ref, false);

      as.write_label("got_cell_code");
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
  this->write_jump_to_cell(iterator_pos, as, iterator_pos.copy().move_forward());

  as.write_label("opcode_end_alignment_changed");
  this->write_jump_to_cell(iterator_pos, as,
      iterator_pos.copy().move_forward().change_alignment());

  as.write_label("opcode_end_zero_count_alignment_same");
  this->write_jump_to_cell(iterator_pos, as, target_pos.copy().move_forward());

  as.write_label("opcode_end_zero_count_alignment_changed");
  this->write_jump_to_cell(iterator_pos, as,
      target_pos.copy().move_forward().change_alignment());
}

const void* BefungeJITCompiler::compile_cell(const Position& cell_pos,
    bool reset_cell) {
  // we'll compile the given cell, and any other cells that depend on its
  // address (but only if its address changes)
  set<Position> pending_positions({cell_pos});
  while (!pending_positions.empty()) {
    const Position pos = pending_positions.begin()->copy();
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

        // special registers:
        // r12 = common object ptr
        // r13 = stack end ptr - 8 (address of top item on the stack)
        as.write_push(r12);
        as.write_mov(r12, reinterpret_cast<int64_t>(this->common_objects.data()));
        as.write_push(r13);
        as.write_lea(r13, MemoryReference(rsp, -8));

        this->write_jump_to_cell(pos, as, Position(0, 0, Direction::Right, true));

      } else {
        opcode = this->field.get(pos.x, pos.y);

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
      data = as.assemble(patch_offsets, &label_offsets);
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

    if (this->debug_flags & DebugFlag::ShowAssembledCells) {
      if (reset_cell) {
        fprintf(stderr, "reset cell (%zu, %zd, %zd, %s, %s) (opcode = %02hX \'%c\')\n",
            pos.special_cell_id, pos.x, pos.y, name_for_direction(pos.dir),
            pos.stack_aligned ? "aligned" : "misaligned", opcode, opcode);
      } else {
        string dasm = AMD64Assembler::disassemble(cell.code, cell.code_size,
            reinterpret_cast<uint64_t>(cell.code), &label_offsets);
        fprintf(stderr, "compiled cell (%zu, %zd, %zd, %s, %s) (opcode = %02hX \'%c\'):\n%s\n\n",
            pos.special_cell_id, pos.x, pos.y, name_for_direction(pos.dir),
            pos.stack_aligned ? "aligned" : "misaligned", opcode, opcode,
            dasm.c_str());
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

void BefungeJITCompiler::write_jump_to_cell(const Position& current_pos,
    AMD64Assembler& as, const Position& next_pos) {
  Position& next_pos_norm = next_pos.copy().wrap_to_field(this->field);
  auto& next_cell = this->compiled_cells[next_pos_norm];
  if (next_cell.code) {
    as.write_jmp(next_cell.code);
  } else {
    // dispatch_compile_cell returns the newly-compiled cell's entry point, so
    // we can just jump to that
    as.write_mov(rdi, this->common_object_reference("this"));
    as.write_mov(rsi, next_pos_norm.x);
    as.write_mov(rdx, next_pos_norm.y);
    as.write_mov(rcx, next_pos_norm.dir);
    as.write_mov(r8, next_pos_norm.stack_aligned);
    if (next_pos_norm.stack_aligned) {
      as.write_push(this->common_object_reference("dispatch_compile_cell_ret_aligned"));
    } else {
      as.write_sub(rsp, 8);
      as.write_push(this->common_object_reference("dispatch_compile_cell_ret_misaligned"));
    }
    as.write_jmp(this->common_object_reference("dispatch_compile_cell"));
  }

  next_cell.address_dependencies.emplace(current_pos);
}

void BefungeJITCompiler::write_direction_jump_table(AMD64Assembler& as,
    const string& label_name, const Position& pos,
    const vector<Direction>& dirs) {
  vector<int64_t> jump_table_contents;
  for (Direction dir : dirs) {
    Position next_pos = pos.copy().face_and_move(dir).wrap_to_field(this->field);
    CompiledCell& next_cell = this->compiled_cells[next_pos];
    if (next_cell.code) {
      jump_table_contents.emplace_back(reinterpret_cast<int64_t>(
          next_cell.code));
      next_cell.address_dependencies.emplace(pos);
    } else {
      as.write_label(label_name + "_" + name_for_direction(dir));
      this->write_jump_to_cell(pos, as, next_pos);
      jump_table_contents.emplace_back(0);
    }
  }

  as.write_label(label_name);
  for (size_t x = 0; x < dirs.size(); x++) {
    Direction dir = dirs[x];
    if (!jump_table_contents[x]) {
      as.write_label_address(label_name + "_" + name_for_direction(dir));
    } else {
      as.write_raw(&jump_table_contents[x], 8);
    }
  }
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

const void* BefungeJITCompiler::dispatch_compile_cell(BefungeJITCompiler* c,
    ssize_t x, ssize_t y, Direction dir, bool stack_aligned) {
  const void* ret = c->compile_cell(Position(x, y, dir, stack_aligned));
  if (c->debug_flags & DebugFlag::ShowAssembledCells) {
    fprintf(stderr, "returning control to compiled code at %016" PRIX64 "\n",
        reinterpret_cast<uint64_t>(ret));
  }
  return ret;
}

const void* BefungeJITCompiler::dispatch_get_cell_code(BefungeJITCompiler* c,
    ssize_t x, ssize_t y, Direction dir, bool stack_aligned) {
  Position pos(x, y, dir, stack_aligned);
  pos.wrap_to_field(c->field);
  try {
    const void* code = c->compiled_cells.at(pos).code;
    if (code) {
      return code;
    }
  } catch (const out_of_range&) { }

  return c->compile_cell(pos);
}

int64_t BefungeJITCompiler::dispatch_field_read(BefungeJITCompiler* c,
    ssize_t x, ssize_t y) {
  return c->field.get(x, y);
}

const void* BefungeJITCompiler::dispatch_field_write(BefungeJITCompiler* c,
    int64_t return_position_token, ssize_t x, ssize_t y, int64_t value) {
  c->field.set(x, y, value);

  // reset all the cells that this could affect
  // TODO: this can be made more efficient by tracking which cells depend on
  // this cell (even if it's a space), or the field edge location, and only
  // resetting those cells
  Position pos(x, y, Direction::Left, false);
  for (Direction dir : all_directions) {
    pos.face(dir);
    c->compile_cell(pos, true);
    pos.change_alignment();
    c->compile_cell(pos, true);
  }

  const auto& return_position = c->token_to_position.at(return_position_token);
  auto& return_cell = c->compiled_cells[return_position];
  const void* ret = return_cell.code ? return_cell.code : c->compile_cell(return_position);
  if (c->debug_flags & DebugFlag::ShowAssembledCells) {
    fprintf(stderr, "returning control to compiled code at %016" PRIX64 "\n",
        reinterpret_cast<uint64_t>(ret));
  }
  return ret;
}

void BefungeJITCompiler::dispatch_print_stack_contents(const int64_t* stack_top,
    size_t count) {
  fprintf(stderr, "[stack debug: %zu items; item 0 at top]\n", count);
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

void BefungeJITCompiler::dispatch_throw_error(const char* message) {
  throw runtime_error(message);
}
