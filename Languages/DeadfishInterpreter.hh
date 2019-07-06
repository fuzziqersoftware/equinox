#pragma once

#include <stddef.h>

#include <string>



class DeadfishInterpreter {
public:
  explicit DeadfishInterpreter(const std::string& filename, bool ascii);
  ~DeadfishInterpreter() = default;

  void execute();

private:
  std::string code;
  bool ascii;
};
