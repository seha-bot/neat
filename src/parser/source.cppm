module;

#include <cstddef>
#include <ostream>

export module source;

export namespace source {

struct Position {
  friend std::ostream &operator<<(std::ostream &os, Position const &sl) {
    return os << sl.line << ':' << sl.col;
  }

  std::size_t line, col;
};

struct Range {
  friend std::ostream &operator<<(std::ostream &os, Range const &sr) {
    return os << '[' << sr.first << ',' << sr.last << ']';
  }

  Position first;
  Position last;
};

} // namespace source
