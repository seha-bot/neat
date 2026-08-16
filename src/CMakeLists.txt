add_library(neat_lib)
target_sources(neat_lib
  PUBLIC
    FILE_SET CXX_MODULES
    FILES
      common/entity.cppm
      common/expr.cppm
      common/formatter.cppm
      common/id.cppm
      common/move_only_vector.cppm
      common/tag.cppm
      common/todo.cppm
      common/type.cppm
      common/type_storage.cppm
      parser/ast.cppm
      parser/parser.cppm
      parser/raw_parser.cppm
      parser/scope.cppm
      parser/tag_storage.cppm
      parser/token.cppm
      typechecker/constraint.cppm
      typechecker/typechecker.cppm
      typechecker/typed_expr.cppm
  PUBLIC
    FILE_SET HEADERS
    FILES
      common/try.hpp
)
target_link_libraries(neat_lib PUBLIC compiler_flags)

add_executable(neat main.cpp)
target_link_libraries(neat PRIVATE
  neat_lib
)
