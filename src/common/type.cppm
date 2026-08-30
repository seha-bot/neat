module;

#include <cstddef>
#include <variant>

export module type;

import id;
import move_only_vector;

export namespace kind {

struct Arrow {
  id::KindId from_id;
  id::KindId to_id;
};

struct Type {};

using KindBase = std::variant<Arrow, Type>;

struct Kind : KindBase {
  using KindBase::KindBase;
};

} // namespace kind

export namespace type {

struct Arrow {
  id::TypeId from_id;
  id::TypeId to_id;
};

// This is indexed by De Bruijn indices to simplify merging.
struct ForAll {
  id::KindId binding_kind_id;
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

// This is also indexed by De Bruijn indices.
struct TTLambda {
  id::KindId binding_kind_id;
  id::TypeId type_id;
};

struct Application {
  id::TypeId function_id;
  id::TypeId argument_id;
};

/// Each object represents a unique variable.
struct Variable {
  id::KindId kind_id;
};

/// These variables can only be merged with itself.
struct RigidVariable {
  id::KindId kind_id;
};

struct NamedTypeReference {
  id::FormId form_id;
};

using TypeBase = std::variant<Arrow, ForAll, DeBruijnIndex, Union, Struct, TTLambda, Application,
                              Variable, RigidVariable, NamedTypeReference>;
struct Type : TypeBase {
  using TypeBase::TypeBase;
};

} // namespace type

// module;
//
// #include <cstddef>
// #include <variant>
//
// export module type;
//
// import id;
// import move_only_vector;
//
// export namespace kind {
//
// struct Arrow {
//   id::KindId from_id;
//   id::KindId to_id;
// };
//
// struct Type {};
//
// using KindBase = std::variant<Arrow, Type>;
//
// struct Kind : KindBase {
//   using KindBase::KindBase;
// };
//
// } // namespace kind
//
// export namespace type {
//
// struct FuckYOU {
//   id::KindId kind_id;
// };
//
// struct Arrow {
//   id::TypeId from_id;
//   id::TypeId to_id;
// };
//
// // This is indexed by De Bruijn indices to simplify merging.
// struct ForAll {
//   id::TypeId type_id;
// };
//
// struct DeBruijnIndex : FuckYOU {
//   std::size_t value;
// };
//
// struct Element {
//   id::TagId tag_id;
//   id::TypeId type_id;
// };
//
// struct Union {
//   move_only_vector<Element> elements;
// };
//
// struct Struct {
//   move_only_vector<Element> elements;
// };
//
// // This is also indexed by De Bruijn indices.
// struct TTLambda : FuckYOU {
//   id::TypeId type_id;
// };
//
// struct Application : FuckYOU {
//   id::TypeId function_id;
//   id::TypeId argument_id;
// };
//
// /// Each object represents a unique variable.
// struct Variable : FuckYOU {};
//
// /// These variables can only be merged with itself.
// struct RigidVariable : FuckYOU {};
//
// struct NamedTypeReference : FuckYOU {
//   id::FormId form_id;
// };
//
// using TypeBase = std::variant<Arrow, ForAll, DeBruijnIndex, Union, Struct, TTLambda, Application,
//                               Variable, RigidVariable, NamedTypeReference>;
// struct Type : TypeBase {
//   using TypeBase::TypeBase;
// };
//
// } // namespace type
