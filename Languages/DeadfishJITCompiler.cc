#include "DeadfishJITCompiler.hh"

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

#include <libamd64/AMD64Assembler.hh>
#include <libamd64/CodeBuffer.hh>

#include "Common.hh"

using namespace std;



static inline bool is_command(char cmd) {
  return (cmd == 'i') || (cmd == 'd') || (cmd == 's') || (cmd == 'o');
}

DeadfishJITCompiler::DeadfishJITCompiler(const string& filename,
    bool ascii, uint64_t debug_flags) : code(load_file(filename)), ascii(ascii),
    debug_flags(debug_flags) {
  // strip all the non-opcode data out of the code
  char* write_ptr = const_cast<char*>(this->code.data());
  for (char ch : this->code) {
    if (is_command(ch)) {
      *(write_ptr++) = ch;
    }
  }
  this->code.resize(write_ptr - this->code.data());
}



void DeadfishJITCompiler::dispatch_output(int64_t value) {
  fprintf(stdout, "%" PRId64 "\n", value);
}

void DeadfishJITCompiler::write_check_value(AMD64Assembler& as,
    const string& label_prefix) {
  string reset_label = label_prefix + "_reset_value";
  string skip_label = label_prefix + "_skip_reset_value";
  as.write_cmp(r13, r14);
  as.write_je(reset_label);
  as.write_cmp(r13, r15);
  as.write_je(reset_label);
  as.write_jmp(skip_label);
  as.write_label(reset_label);
  as.write_xor(r13, r13);
  as.write_label(skip_label);
}

void DeadfishJITCompiler::execute() {
  AMD64Assembler as;

  // r12 = output function ptr
  // r13 = value
  // r14 = -1
  // r15 = 0x100

  // generate lead-in code
  as.write_push(rbp);
  as.write_mov(rbp, rsp);
  as.write_push(r12);
  as.write_push(r13);
  as.write_push(r14);
  as.write_push(r15);
  if (this->ascii) {
    as.write_mov(r12, reinterpret_cast<int64_t>(&putchar));
  } else {
    as.write_mov(r12, reinterpret_cast<int64_t>(&DeadfishJITCompiler::dispatch_output));
  }
  as.write_xor(r13, r13);
  as.write_mov(r14, -1);
  as.write_mov(r15, 0x100);

  // generate program assembly
  size_t count = 0;
  for (size_t offset = 0; offset < this->code.size(); offset += count) {
    char opcode = this->code[offset];
    count = 1;
    for (; (offset + count < this->code.size()) && (this->code[offset + count] == opcode); count++);

    switch (opcode) {
      case 'i':
        // TODO: optimize this by collapsing sequential incrs into adds
        for (size_t x = 0; x < count; x++) {
          as.write_inc(r13);
          this->write_check_value(as, string_printf("%zu_%zu", offset, x));
        }
        break;

      case 'd':
        // TODO: optimize this by collapsing sequential incrs into adds
        for (size_t x = 0; x < count; x++) {
          as.write_dec(r13);
          this->write_check_value(as, string_printf("%zu_%zu", offset, x));
        }
        break;

      case 's':
        for (size_t x = 0; x < count; x++) {
          as.write_imul(r13, r13);
          this->write_check_value(as, string_printf("%zu_%zu", offset, x));
        }
        break;

      case 'o':
        for (size_t x = 0; x < count; x++) {
          as.write_mov(rdi, r13);
          as.write_call(r12);
        }
        break;
    }
  }

  // generate lead-out code
  as.write_pop(r15);
  as.write_pop(r14);
  as.write_pop(r13);
  as.write_pop(r12);
  as.write_pop(rbp);
  as.write_ret();

  // assemble it all
  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  string data = as.assemble(&patch_offsets, &compiled_labels);
  void* executable_data = this->buf.append(data, &patch_offsets);
  void (*function)() = reinterpret_cast<void(*)()>(executable_data);

  if (this->debug_flags & DebugFlag::ShowAssembly) {
    string disassembly = AMD64Assembler::disassemble(executable_data,
        data.size(), reinterpret_cast<int64_t>(executable_data),
        &compiled_labels);
    fprintf(stderr, "%s\n", disassembly.c_str());
    string size_str = format_size(data.size());
    fprintf(stderr, "code buffer size: %s\n", size_str.c_str());
  }

  // run it
  function();
}
