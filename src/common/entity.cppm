module;

#include <optional>
#include <string>

export module entity;

import ast;
import id;
import move_only_vector;

export namespace entity {

struct TypeBinding {
  std::string name;
  ast::kind::Kind kind;
};

struct Binding {
  std::string name;
  id::TypeId type_id;
};

struct TypeDefinition {
  std::string name;
  move_only_vector<TypeBinding> type_bindings;
  id::TypeId type_id;
};

template <typename Expr> struct ValueDefinition {
  std::optional<id::TypeId> type_id;
  std::string name;
  Expr value;
};

} // namespace entity
