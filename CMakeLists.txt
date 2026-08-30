cmake_minimum_required(VERSION 4.3.4)
project(neat VERSION 0.0.1 LANGUAGES CXX)

# Disable standard decaying and disable extensions.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable sanitizers for all targets.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -O0)
else()
  add_compile_options(-fsanitize=address,undefined)
endif()
add_link_options(-fsanitize=address,undefined)

# Common compiler options.
add_library(compiler_flags INTERFACE)
target_compile_features(compiler_flags INTERFACE cxx_std_23)
target_compile_options(compiler_flags INTERFACE -Wall -Wextra -Wpedantic -Wunused -Wshadow -Wconversion -Wsign-conversion)

add_subdirectory(src)

option(BUILD_TESTS "should tests be built" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
