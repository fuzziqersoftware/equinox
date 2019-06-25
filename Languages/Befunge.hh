#pragma once

#include <inttypes.h>

#include <string>
#include <vector>



struct Field {
  std::vector<std::vector<std::string>> planes;
  ssize_t w;
  ssize_t h;

  Field();

  char get(ssize_t x, ssize_t y, ssize_t z) const;
  void set(ssize_t x, ssize_t y, ssize_t z, char value);

  size_t width() const;
  size_t height() const;
  size_t depth() const;

  ssize_t wrap_x(ssize_t x) const;
  ssize_t wrap_y(ssize_t y) const;
  ssize_t wrap_z(ssize_t y) const;

  static Field load(const std::string& filename);
};

struct Position {
  int64_t x;
  int64_t y;
  int64_t z;
  int64_t dx;
  int64_t dy;
  int64_t dz;
  uint8_t stack_aligned;
  uint8_t special_cell_id;

  Position(uint8_t special_cell_id = 0, bool stack_aligned = true);
  Position(int64_t x, int64_t y, int64_t z, int64_t dx, int64_t dy, int64_t dz,
      bool stack_aligned = true);

  Position copy() const;
  Position& face(ssize_t dx, ssize_t dy, ssize_t dz);
  Position& turn_left();
  Position& turn_right();
  Position& turn_around();
  Position& move_forward();
  Position& move_backward();
  Position& change_alignment();
  Position& set_aligned(bool aligned);

  bool is_within_field(const Field& f) const;
  Position& wrap_modulus(const Field& f);
  Position& wrap_lahey(const Field& f);

  std::string str() const;
  std::string label() const;

  bool operator<(const Position& other) const;
};
