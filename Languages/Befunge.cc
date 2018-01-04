#include "Befunge.hh"

#include <inttypes.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <vector>

using namespace std;



Field::Field() : w(0), h(0) { }

char Field::get(ssize_t x, ssize_t y, ssize_t z) const {
  try {
    return this->planes.at(this->wrap_z(z)).at(this->wrap_y(y)).at(this->wrap_x(x));
  } catch (const out_of_range&) {
    return ' ';
  }
}

void Field::set(ssize_t x, ssize_t y, ssize_t z, char value) {
  if (x < 0) {
    x = this->wrap_x(x);
  }
  if (y < 0) {
    y = this->wrap_y(y);
  }
  if (z < 0) {
    z = this->wrap_z(z);
  }

  while (this->planes.size() <= z) {
    this->planes.emplace_back();
  }
  vector<string>& plane = this->planes[z];

  while (plane.size() <= y) {
    plane.emplace_back();
  }
  string& line = plane[y];

  while (line.size() <= x) {
    line.push_back(' ');
  }
  line[x] = value;

  if (x >= this->w) {
    this->w = x + 1;
  }
  if (y >= this->h) {
    this->h = y + 1;
  }
}

size_t Field::width() const {
  return this->w;
}

size_t Field::height() const {
  return this->h;
}

size_t Field::depth() const {
  return this->planes.size();
}

ssize_t Field::wrap_x(ssize_t x) const {
  while (x < 0) {
    x += this->w;
  }
  return x % this->w;
}

ssize_t Field::wrap_y(ssize_t y) const {
  while (y < 0) {
    y += this->h;
  }
  return y % this->h;
}

ssize_t Field::wrap_z(ssize_t z) const {
  while (z < 0) {
    z += this->planes.size();
  }
  return z % static_cast<ssize_t>(this->planes.size());
}

Field Field::load(const string& filename) {
  string code = load_file(filename);

  Field f;
  f.planes.emplace_back();
  vector<string>& plane = f.planes.back();

  size_t line_start_offset = 0;
  size_t newline_offset = code.find('\n', line_start_offset);
  while (newline_offset != string::npos) {
    size_t line_length = newline_offset - line_start_offset;
    plane.emplace_back(code.substr(line_start_offset, line_length));
    line_start_offset = newline_offset + 1;
    newline_offset = code.find('\n', line_start_offset);
  }

  plane.emplace_back(code.substr(line_start_offset));

  for (string& line : plane) {
    if (!line.empty() && (line[line.size() - 1] == '\r')) {
      line.pop_back();
    }
    if (line.size() > f.w) {
      f.w = line.size();
    }
  }
  f.h = plane.size();

  return f;
}



Position::Position(uint8_t special_cell_id, bool stack_aligned) :
    x(0), y(0), z(0), dx(0), dy(0), dz(0), stack_aligned(stack_aligned),
    special_cell_id(special_cell_id) { }
Position::Position(int64_t x, int64_t y, int64_t z, int64_t dx, int64_t dy,
    int64_t dz, bool stack_aligned) : x(x), y(y), z(z), dx(dx), dy(dy), dz(dz),
    stack_aligned(stack_aligned), special_cell_id(0) { }

Position Position::copy() const {
  return *this;
}

Position& Position::face(ssize_t dx, ssize_t dy, ssize_t dz) {
  this->dx = dx;
  this->dy = dy;
  this->dz = dz;
  return *this;
}

Position& Position::turn_left() {
  ssize_t new_dy = -this->dx;
  this->dx = this->dy;
  this->dy = new_dy;
  return *this;
}

Position& Position::turn_right() {
  ssize_t new_dy = this->dx;
  this->dx = -this->dy;
  this->dy = new_dy;
  return *this;
}

Position& Position::turn_around() {
  this->dx *= -1;
  this->dy *= -1;
  this->dz *= -1;
  return *this;
}

Position& Position::move_forward() {
  this->x += this->dx;
  this->y += this->dy;
  this->z += this->dz;
  return *this;
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

string Position::str() const {
  if (this->special_cell_id) {
    return string_printf("(Special, %zu)", this->special_cell_id);
  }
  return string_printf("(%zd, %zd, %zd, %zd, %zd, %zd, %s)", this->x, this->y,
      this->z, this->dx, this->dy, this->dz,
      this->stack_aligned ? "aligned" : "misaligned");
}

string Position::label() const {
  if (this->special_cell_id) {
    return string_printf("Special_%zu", this->special_cell_id);
  }
  return string_printf("%zd_%zd_%zd_%zd_%zd_%zd_%s", this->x, this->y, this->z,
      this->dx, this->dy, this->dz,
      this->stack_aligned ? "aligned" : "misaligned");
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
  if (this->z < other.z) {
    return true;
  } else if (this->z > other.z) {
    return false;
  }
  if (this->dx < other.dx) {
    return true;
  } else if (this->dx > other.dx) {
    return false;
  }
  if (this->dy < other.dy) {
    return true;
  } else if (this->dy > other.dy) {
    return false;
  }
  if (this->dz < other.dz) {
    return true;
  } else if (this->dz > other.dz) {
    return false;
  }
  return (this->stack_aligned < other.stack_aligned);
}
