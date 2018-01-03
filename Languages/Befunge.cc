#include "Befunge.hh"

#include <inttypes.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <vector>

using namespace std;



const vector<Direction> all_directions({
    Direction::Left, Direction::Up, Direction::Right, Direction::Down});

const char* name_for_direction(Direction d) {
  switch (d) {
    case Right:
      return "Right";
    case Left:
      return "Left";
    case Down:
      return "Down";
    case Up:
      return "Up";
  }
  return "Unknown";
}



Field::Field() : w(0) { }

char Field::get(ssize_t x, ssize_t y) const {
  try {
    return this->lines[this->wrap_y(y)].at(this->wrap_x(x));
  } catch (const out_of_range&) {
    return ' ';
  }
}

void Field::set(ssize_t x, ssize_t y, char value) {
  if (x < 0) {
    x = this->wrap_x(x);
  }
  if (y < 0) {
    y = this->wrap_y(y);
  }

  while (this->lines.size() <= y) {
    this->lines.emplace_back();
  }
  string& line = this->lines[y];

  while (line.size() <= x) {
    line.push_back(' ');
  }
  line[x] = value;

  if (x >= this->w) {
    this->w = x + 1;
  }
}

size_t Field::width() const {
  return this->w;
}

size_t Field::height() const {
  return this->lines.size();
}

ssize_t Field::wrap_x(ssize_t x) const {
  while (x < 0) {
    x += this->w;
  }
  return x % this->w;
}

ssize_t Field::wrap_y(ssize_t y) const {
  while (y < 0) {
    y += this->lines.size();
  }
  return y % static_cast<ssize_t>(this->lines.size());
}

Field Field::load(const string& filename) {
  string code = load_file(filename);

  Field f;
  size_t line_start_offset = 0;
  size_t newline_offset = code.find('\n', line_start_offset);
  while (newline_offset != string::npos) {
    size_t line_length = newline_offset - line_start_offset;
    f.lines.emplace_back(code.substr(line_start_offset, line_length));
    line_start_offset = newline_offset + 1;
    newline_offset = code.find('\n', line_start_offset);
  }

  f.lines.emplace_back(code.substr(line_start_offset));

  for (string& line : f.lines) {
    if (!line.empty() && (line[line.size() - 1] == '\r')) {
      line.pop_back();
    }
    if (line.size() > f.w) {
      f.w = line.size();
    }
  }

  return f;
}



Position::Position(size_t special_cell_id, bool stack_aligned) :
    special_cell_id(special_cell_id), x(0), y(0), dir(Direction::Right),
    stack_aligned(stack_aligned) { }
Position::Position(ssize_t x, ssize_t y, Direction dir, bool stack_aligned) :
    special_cell_id(0), x(x), y(y), dir(dir), stack_aligned(stack_aligned) { }

Position Position::copy() const {
  return *this;
}

Position& Position::face(Direction dir) {
  this->dir = dir;
  return *this;
}

Position& Position::turn_left() {
  switch (this->dir) {
    case Direction::Left:
      this->dir = Direction::Down;
      break;
    case Direction::Right:
      this->dir = Direction::Up;
      break;
    case Direction::Up:
      this->dir = Direction::Left;
      break;
    case Direction::Down:
      this->dir = Direction::Right;
      break;
  }
  return *this;
}

Position& Position::turn_right() {
  switch (this->dir) {
    case Direction::Left:
      this->dir = Direction::Up;
      break;
    case Direction::Right:
      this->dir = Direction::Down;
      break;
    case Direction::Up:
      this->dir = Direction::Right;
      break;
    case Direction::Down:
      this->dir = Direction::Left;
      break;
  }
  return *this;
}

Position& Position::turn_around() {
  switch (this->dir) {
    case Direction::Left:
      this->dir = Direction::Right;
      break;
    case Direction::Right:
      this->dir = Direction::Left;
      break;
    case Direction::Up:
      this->dir = Direction::Down;
      break;
    case Direction::Down:
      this->dir = Direction::Up;
      break;
  }
  return *this;
}

Position& Position::move_forward() {
  switch (this->dir) {
    case Direction::Left:
      this->x--;
      break;
    case Direction::Right:
      this->x++;
      break;
    case Direction::Up:
      this->y--;
      break;
    case Direction::Down:
      this->y++;
      break;
  }
  return *this;
}

Position& Position::face_and_move(Direction dir) {
  return this->face(dir).move_forward();
}

Position& Position::change_alignment() {
  this->stack_aligned = !this->stack_aligned;
  return *this;
}

Position& Position::set_aligned(bool aligned) {
  this->stack_aligned = aligned;
  return *this;
}

Position& Position::wrap_to_field(const Field& f) {
  this->x = f.wrap_x(this->x);
  this->y = f.wrap_y(this->y);
  return *this;
}

string Position::label() const {
  if (this->special_cell_id) {
    return string_printf("Special_%zu", this->special_cell_id);
  }
  return string_printf("%zd_%zd_%s_%s", this->x, this->y,
      name_for_direction(this->dir), this->stack_aligned ? "aligned" : "misaligned");
}

bool Position::operator<(const Position& other) const {
  if (this->special_cell_id < other.special_cell_id) {
    return true;
  } else if (this->special_cell_id > other.special_cell_id) {
    return false;
  }
  if (this->x < other.x) {
    return true;
  } else if (this->x > other.x) {
    return false;
  }
  if (this->y < other.y) {
    return true;
  } else if (this->y > other.y) {
    return false;
  }
  if (this->dir < other.dir) {
    return true;
  } else if (this->dir > other.dir) {
    return false;
  }
  return (this->stack_aligned < other.stack_aligned);
}
