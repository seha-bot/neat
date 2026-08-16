module;

#include <cstddef>
#include <variant>

export module type;

import id;
import move_only_vector;

export namespace type {

struct Arrow {
  id::TypeId from_id;
  id::TypeId to_id;
};

// This is indexed by De Bruijn indices to simplify merging.
struct ForAll {
  id::TypeId type_id;
};

struct DeBruijnIndex {
  std::size_t value;
};

struct Element {
  id::TagId tag_id;
  id::TypeId type_id;
};

struct Union {
  move_only_vector<Element> elements;
};

struct Struct {
  move_only_vector<Element> elements;
};

struct Application {
  id::TypeId function_id;
  id::TypeId argument_id;
};

// Each object represents a unique variable.
struct Variable {};

struct NamedTypeReference {
  id::FormId form_id;
};

using TypeBase = std::variant<Arrow, ForAll, DeBruijnIndex, Union, Struct, Application, Variable,
                              NamedTypeReference>;
struct Type : TypeBase {
  using TypeBase::TypeBase;
};

} // namespace type
