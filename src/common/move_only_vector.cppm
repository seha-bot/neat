module;

#include <memory>
#include <vector>

export module move_only_vector;

template <class T, class Allocator> using base = std::vector<T, Allocator>;

export template <class T, class Allocator = std::allocator<T>>
struct move_only_vector : base<T, Allocator> {
  using base<T, Allocator>::base;

  move_only_vector(move_only_vector &&) = default;
  move_only_vector(move_only_vector const &) = delete;

  move_only_vector &operator=(move_only_vector &&) = default;
  move_only_vector &operator=(move_only_vector const &) = delete;
};
