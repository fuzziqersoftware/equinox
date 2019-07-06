#pragma once

#include <stddef.h>

#include <libamd64/AMD64Assembler.hh>
#include <libamd64/CodeBuffer.hh>



class DeadfishJITCompiler {
public:
  explicit DeadfishJITCompiler(const std::string& filename,
      bool ascii, uint64_t debug_flags = 0);
  ~DeadfishJITCompiler() = default;

  void execute();

private:
  static void dispatch_output(int64_t value);
  void write_check_value(AMD64Assembler& as, const std::string& label_prefix);

  std::string code;
  bool ascii;
  uint64_t debug_flags;

  CodeBuffer buf;
};
