#include "DeadfishInterpreter.hh"

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



DeadfishInterpreter::DeadfishInterpreter(const string& filename,
    bool ascii) : code(load_file(filename)), ascii(ascii) { }

void DeadfishInterpreter::execute() {
  int64_t value = 0;
  for (char ch : this->code) {
    switch (ch) {
      case 'i':
        value++;
        break;
      case 'd':
        value--;
        break;
      case 's':
        value *= value;
        break;
      case 'o':
        if (ascii) {
          putchar(value);
        } else {
          fprintf(stdout, "%" PRId64 "\n", value);
        }
        break;
    }
    if (value == -1 || value == 256) {
      value = 0;
    }
  }
}
