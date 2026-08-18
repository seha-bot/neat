module;

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

export module ast;

import move_only_vector;

export namespace ast {

namespace kind {

struct Kind;

struct Arrow {
  bool operator==(Arrow const &that) const;
  Arrow clone() const;

  std::unique_ptr<Kind> from, to;
};

struct Type {
  bool operator==(Type const &) const { return true; }
  Type clone() const { return Type{}; }
};

using KindBase = std::variant<Arrow, Type>;

struct Kind : KindBase {
  using KindBase::KindBase;

  Kind clone() const {
    return std::visit([](auto &x) -> Kind { return x.clone(); }, *this);
  }
};

bool Arrow::operator==(Arrow const &that) const { return *from == *that.from and *to == *that.to; }
Arrow Arrow::clone() const {
  return Arrow{
      std::make_unique<Kind>(from->clone()),
      std::make_unique<Kind>(to->clone()),
  };
}

} // namespace kind

namespace type {

struct Type;

struct Arrow {
  std::unique_ptr<Type> from;
  std::unique_ptr<Type> to;
};

struct Binding {
  std::string_view name;
  kind::Kind kind;
};

struct ForAll {
  Binding binding;
  std::unique_ptr<Type> type;
};

struct Element;

struct Union {
  move_only_vector<Element> elements;
};

struct Struct {
  move_only_vector<Element> elements;
};

struct Application {
  std::unique_ptr<Type> function;
  std::unique_ptr<Type> argument;
};

struct NamedTypeOrTypeBindingReference {
  std::string_view name;
};

using TypeBase =
    std::variant<Arrow, ForAll, Union, Struct, Application, NamedTypeOrTypeBindingReference>;

struct Type : TypeBase {
  using TypeBase::TypeBase;
};

struct Element {
  std::string_view tag;
  Type type;
};

} // namespace type

namespace pattern {

struct Pattern;

struct TaggedValue {
  std::string_view tag;
  std::unique_ptr<Pattern> value;
};

struct Pack {
  move_only_vector<TaggedValue> tagged_values;
};

struct Binding {
  std::string_view name;
};

using PatternBase = std::variant<TaggedValue, Pack, Binding>;
struct Pattern : PatternBase {
  using PatternBase::PatternBase;
};

} // namespace pattern

namespace expr {

struct Expr;

struct Application {
  std::unique_ptr<Expr> function, argument;
};

struct Choice {
  pattern::Pattern pattern;
  std::unique_ptr<Expr> result;
};

struct Case {
  std::unique_ptr<Expr> scrutinee;
  move_only_vector<Choice> choices;
};

struct TaggedValue {
  std::string_view tag;
  std::unique_ptr<Expr> value;
};

struct Pack {
  move_only_vector<TaggedValue> tagged_values;
};

struct Binding {
  std::string_view name;
  std::optional<type::Type> type;
};

struct Lambda {
  Binding binding;
  std::unique_ptr<Expr> body;
};

struct TVLambda {
  type::Binding type_binding;
  std::unique_ptr<Expr> body;
};

struct ValueOrBindingReference {
  std::string_view name;
};

using ExprBase =
    std::variant<Application, Case, TaggedValue, Pack, Lambda, TVLambda, ValueOrBindingReference>;

struct Expr : ExprBase {
  using ExprBase::ExprBase;
};

} // namespace expr

namespace entity {

struct TypeDefinition {
  std::string_view name;
  move_only_vector<type::Binding> type_bindings;
  type::Type type;
};

struct ValueDefinition {
  std::optional<type::Type> type;
  std::string_view name;
  move_only_vector<type::Binding> type_bindings;
  move_only_vector<expr::Binding> bindings;
  expr::Expr value;
};

using EntityBase = std::variant<TypeDefinition, ValueDefinition>;

struct Entity : EntityBase {
  using EntityBase::EntityBase;
};

} // namespace entity

} // namespace ast
