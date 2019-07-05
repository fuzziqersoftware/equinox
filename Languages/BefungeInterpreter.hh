#pragma once

#include <string>
#include <vector>

#include "Befunge.hh"

class BefungeInterpreter {
public:
  explicit BefungeInterpreter(const std::string& filename,
      uint8_t dimensions = 2);
  ~BefungeInterpreter() = default;

  void execute();

private:
  template <typename T> struct Stack {
    std::vector<T> stack;

    void push(const T& i) {
      this->stack.emplace_back(i);
    }

    void push(T&& i) {
      this->stack.emplace_back(std::move(i));
    }

    T pop() {
      if (this->stack.empty()) {
        return 0;
      }
      T i = std::move(this->stack.back());
      this->stack.pop_back();
      return i;
    }

    size_t size() const {
      return this->stack.size();
    }

    T& at(size_t x) {
      return this->stack.at(this->stack.size() - x - 1);
    }
  };

  Stack<Stack<int64_t>> ss;
  Stack<int64_t>* s;

  Field field;
  Position pos;
  uint8_t dimensions;

  void execute_opcode(int16_t opcode);
};
