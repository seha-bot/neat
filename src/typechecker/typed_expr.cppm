module;

#include <functional>
#include <memory>
#include <variant>

export module typed_expr;

import entity;
import expr;
import id;
import move_only_vector;

export namespace typed_expr {

struct Expr;

struct Typed {
  id::TypeId type_id;
};

struct Application : Typed {
  std::unique_ptr<Expr> function;
  std::unique_ptr<Expr> argument;
};

struct Choice;

struct Case : Typed {
  std::unique_ptr<Expr> scrutinee;
  move_only_vector<Choice> choices;
};

struct TaggedValue : Typed {
  id::TagId tag_id;
  std::unique_ptr<Expr> value;
};

struct Pack : Typed {
  move_only_vector<TaggedValue> tagged_values;
};

struct Lambda : Typed {
  move_only_vector<std::reference_wrapper<entity::Binding const>> captures;
  std::unique_ptr<entity::Binding> binding;
  std::unique_ptr<Expr> body;
};

struct TVLambda : Typed {
  std::unique_ptr<Expr> body;
};

struct ValueReference : Typed {
  id::ValueId value_id;
};

struct BindingReference : Typed {
  std::reference_wrapper<entity::Binding const> binding;
};

struct Conversion : Typed {
  std::unique_ptr<Expr> expr;
};

using ExprBase = std::variant<Application, Case, TaggedValue, Pack, Lambda, TVLambda,
                              ValueReference, BindingReference, Conversion>;
struct Expr : ExprBase {
  using ExprBase::ExprBase;

  id::TypeId type_id() const {
    return std::visit([](Typed const &t) { return t.type_id; }, *this);
  }
};

struct Choice {
  expr::pattern::Pattern pattern;
  Expr arm;
};

} // namespace typed_expr
