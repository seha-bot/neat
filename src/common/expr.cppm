module;

#include <functional>
#include <memory>
#include <variant>

export module expr;

import entity;
import id;
import move_only_vector;

export namespace expr {

struct Expr;

struct Application {
  std::unique_ptr<Expr> function;
  std::unique_ptr<Expr> argument;
};

namespace pattern {

struct Pattern;

struct TaggedValue {
  id::TagId tag_id;
  std::unique_ptr<Pattern> value;
};

struct Pack {
  move_only_vector<TaggedValue> tagged_values;
};

struct Binding {
  std::unique_ptr<entity::Binding> binding;
};

using PatternBase = std::variant<TaggedValue, Pack, Binding>;
struct Pattern : PatternBase {
  using PatternBase::PatternBase;
};

} // namespace pattern

struct Choice;

struct Case {
  std::unique_ptr<Expr> scrutinee;
  move_only_vector<Choice> choices;
};

struct TaggedValue {
  id::TagId tag_id;
  std::unique_ptr<Expr> value;
};

struct Pack {
  move_only_vector<TaggedValue> tagged_values;
};

struct Lambda {
  move_only_vector<std::reference_wrapper<entity::Binding const>> captures;
  std::unique_ptr<entity::Binding> binding;
  std::unique_ptr<Expr> body;
};

struct TVLambda {
  id::KindId binding_kind_id;
  std::unique_ptr<Expr> body;
};

struct Instantiation {
  std::unique_ptr<Expr> function;
  id::TypeId argument_id;
};

struct ValueReference {
  id::ValueId value_id;
};

struct BindingReference {
  std::reference_wrapper<entity::Binding const> binding;
};

using ExprBase = std::variant<Application, Case, TaggedValue, Pack, Lambda, TVLambda, Instantiation,
                              ValueReference, BindingReference>;
struct Expr : ExprBase {
  using ExprBase::ExprBase;
};

struct Choice {
  pattern::Pattern pattern;
  Expr result;
};

} // namespace expr
