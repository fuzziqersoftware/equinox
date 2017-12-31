#pragma once

#include <inttypes.h>

#include <string>
#include <vector>



enum Direction {
  Right = 0,
  Left = 1,
  Down = 2,
  Up = 3,
};

extern const std::vector<Direction> all_directions;

const char* name_for_direction(Direction d);

struct Field {
  std::vector<std::string> lines;
  size_t w;

  Field();

  char get(ssize_t x, ssize_t y) const;
  void set(ssize_t x, ssize_t y, char value);

  size_t width() const;
  size_t height() const;

  ssize_t wrap_x(ssize_t x) const;
  ssize_t wrap_y(ssize_t y) const;

  static Field load(const std::string& filename);
};

struct Position {
  size_t special_cell_id;
  ssize_t x;
  ssize_t y;
  Direction dir;
  bool stack_aligned; // blargh system v

  Position(size_t special_cell_id = 0, bool stack_aligned = true);
  Position(ssize_t x, ssize_t y, Direction dir, bool stack_aligned = true);

  Position copy() const;
  Position& face(Direction dir);
  Position& move_forward();
  Position& face_and_move(Direction dir);
  Position& change_alignment();
  Position& wrap_to_field(const Field& f);

  std::string label() const;

  bool operator<(const Position& other) const;
};
