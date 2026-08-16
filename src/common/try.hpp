#ifndef TRY_HPP
#define TRY_HPP

#define TRY_DEF(name, ...)                                                                         \
  auto name = __VA_ARGS__;                                                                         \
  do {                                                                                             \
    if (not name) {                                                                                \
      return std::unexpected(std::move(name).error());                                             \
    }                                                                                              \
  } while (false)

#define TRY(...)                                                                                   \
  do {                                                                                             \
    TRY_DEF(a_very_random_name_that_will_never_clash_with_anything_lol, __VA_ARGS__);              \
  } while (false)

#endif
