module;

#include <expected>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "common/try.hpp"

export module parser;

import ast;
import entity;
import expr;
import id;
import move_only_vector;
import raw_parser;
import scope;
import tag;
import tag_storage;
import todo;
import type;
import type_storage;

namespace parser {

struct ParseError {};

namespace {

struct Context {
  Context with_type_binding(std::string_view name) const {
    std::unordered_map<std::string_view, scope::Entry> names;
    names.insert({name, scope::TypeBinding{type_binding_depth}});
    return {ts, tags, scope::Scope{std::move(names), &scope}, type_binding_depth + 1};
  }

  Context with_binding(entity::Binding const &binding) const {
    std::unordered_map<std::string_view, scope::Entry> names;
    names.insert({binding.name, scope::Binding{binding}});
    return {ts, tags, scope::Scope{std::move(names), &scope}, type_binding_depth};
  }

  type_storage::TypeStorage &ts;
  tag_storage::TagStorage &tags;
  scope::Scope scope;
  std::size_t const type_binding_depth;
};

namespace typey {

std::expected<id::TypeId, ParseError> parse_type(Context &ctx, ast::type::Type type) noexcept;

std::expected<type::Arrow, ParseError> parse_type(Context &ctx, ast::type::Arrow arr) noexcept {
  TRY_DEF(from_id, parse_type(ctx, std::move(*arr.from)));
  TRY_DEF(to_id, parse_type(ctx, std::move(*arr.to)));
  return type::Arrow{
      .from_id = *from_id,
      .to_id = *to_id,
  };
}

std::expected<type::ForAll, ParseError> parse_type(Context &ctx,
                                                   ast::type::ForAll for_all) noexcept {
  auto new_ctx = ctx.with_type_binding(for_all.type_binding);
  TRY_DEF(type_id, parse_type(new_ctx, std::move(*for_all.type)));
  return type::ForAll{.type_id = *type_id};
}

std::expected<type::Union, ParseError> parse_type(Context &ctx, ast::type::Union union_) noexcept {
  move_only_vector<type::Element> elements;
  for (auto &[tag, type] : union_.elements) {
    TRY_DEF(type_id, parse_type(ctx, std::move(type)));
    elements.push_back({
        .tag_id = ctx.tags.get_tag(tag),
        .type_id = *type_id,
    });
  }
  return type::Union{.elements = std::move(elements)};
}

std::expected<type::Struct, ParseError> parse_type(Context &ctx,
                                                   ast::type::Struct struct_) noexcept {
  move_only_vector<type::Element> elements;
  for (auto &[tag, type] : struct_.elements) {
    TRY_DEF(type_id, parse_type(ctx, std::move(type)));
    elements.push_back({
        .tag_id = ctx.tags.get_tag(tag),
        .type_id = *type_id,
    });
  }
  return type::Struct{.elements = std::move(elements)};
}

std::expected<type::Application, ParseError> parse_type(Context &ctx,
                                                        ast::type::Application app) noexcept {
  TRY_DEF(function_id, parse_type(ctx, std::move(*app.function)));
  TRY_DEF(argument_id, parse_type(ctx, std::move(*app.argument)));
  return type::Application{
      .function_id = *function_id,
      .argument_id = *argument_id,
  };
}

std::expected<type::Type, ParseError>
parse_type(Context &ctx, ast::type::NamedTypeOrTypeBindingReference ref) noexcept {
  auto scope_entry = ctx.scope.lookup(ref.name);
  if (not scope_entry) {
    todo();
  }

  if (auto *form = std::get_if<scope::TypeDefinition>(&*scope_entry)) {
    return type::NamedTypeReference{.form_id = form->form_id};
  }
  if (auto *type_binding = std::get_if<scope::TypeBinding>(&*scope_entry)) {
    return type::DeBruijnIndex{.value = ctx.type_binding_depth - 1 - type_binding->absolute_index};
  }

  todo();
}

std::expected<id::TypeId, ParseError> parse_type(Context &ctx, ast::type::Type type) noexcept {
  auto visitor = [&](auto a_type) -> std::expected<id::TypeId, ParseError> {
    TRY_DEF(parsed_type, parse_type(ctx, std::move(a_type)));
    return ctx.ts.store(*std::move(parsed_type));
  };
  return std::visit(visitor, std::move(type));
}

} // namespace typey

namespace patterny {

template <typename T>
using Result =
    std::expected<std::pair<std::unordered_map<std::string_view, scope::Entry>, T>, ParseError>;

Result<expr::pattern::Pattern> parse_pattern(Context &ctx, ast::pattern::Pattern pattern) noexcept;

Result<expr::pattern::TaggedValue> parse_pattern(Context &ctx,
                                                 ast::pattern::TaggedValue tagged_value) noexcept {
  TRY_DEF(result, parse_pattern(ctx, std::move(*tagged_value.value)));
  auto [names, value] = *std::move(result);

  expr::pattern::TaggedValue parsed_tagged_value{
      .tag_id = ctx.tags.get_tag(tagged_value.tag),
      .value = std::make_unique<expr::pattern::Pattern>(std::move(value)),
  };

  return std::make_pair(std::move(names), std::move(parsed_tagged_value));
}

Result<expr::pattern::Pack> parse_pattern(Context &ctx, ast::pattern::Pack pack) noexcept {
  std::unordered_map<std::string_view, scope::Entry> names;
  move_only_vector<expr::pattern::TaggedValue> tagged_values;

  for (auto &tagged_value : pack.tagged_values) {
    TRY_DEF(result, parse_pattern(ctx, std::move(tagged_value)));
    auto [new_names, parsed_tagged_value] = *std::move(result);
    for (auto [name, scope_entry] : new_names) {
      if (names.contains(name)) {
        todo();
      }
      names.insert({name, scope_entry});
    }
    tagged_values.push_back(std::move(parsed_tagged_value));
  }

  auto parsed_pack = expr::pattern::Pack{
      .tagged_values = std::move(tagged_values),
  };

  return std::make_pair(std::move(names), std::move(parsed_pack));
}

Result<expr::pattern::Binding> parse_pattern(Context &ctx, ast::pattern::Binding binding) noexcept {
  expr::pattern::Binding parsed_binding{
      .binding = std::make_unique<entity::Binding>(entity::Binding{
          .name = static_cast<std::string>(binding.name),
          .type_id = ctx.ts.make_variable(),
      }),
  };

  std::unordered_map<std::string_view, scope::Entry> names;
  names.insert({parsed_binding.binding->name, scope::Binding{*parsed_binding.binding}});

  return std::make_pair(std::move(names), std::move(parsed_binding));
}

Result<expr::pattern::Pattern> parse_pattern(Context &ctx, ast::pattern::Pattern pattern) noexcept {
  auto visitor = [&](auto a_pattern) -> Result<expr::pattern::Pattern> {
    return parse_pattern(ctx, std::move(a_pattern));
  };
  return std::visit(visitor, std::move(pattern));
}

} // namespace patterny

namespace expry {

// TODO: This makes you wonder if a binding is an entity or an expression or neither?
std::expected<entity::Binding, ParseError> parse_binding(Context &ctx,
                                                         ast::expr::Binding binding) noexcept {
  id::TypeId type_id;
  if (binding.type) {
    TRY_DEF(parsed_type_id, typey::parse_type(ctx, *std::move(binding.type)));
    type_id = *parsed_type_id;
  } else {
    type_id = ctx.ts.make_variable();
  }

  return entity::Binding{
      .name = static_cast<std::string>(binding.name),
      .type_id = type_id,
  };
}

std::expected<expr::Expr, ParseError> parse_expr(Context &ctx, ast::expr::Expr expr) noexcept;

std::expected<expr::Application, ParseError> parse_expr(Context &ctx,
                                                        ast::expr::Application app) noexcept {
  TRY_DEF(function, parse_expr(ctx, std::move(*app.function)));
  TRY_DEF(argument, parse_expr(ctx, std::move(*app.argument)));
  return expr::Application{
      .function = std::make_unique<expr::Expr>(*std::move(function)),
      .argument = std::make_unique<expr::Expr>(*std::move(argument)),
  };
}

std::expected<expr::Case, ParseError> parse_expr(Context &ctx, ast::expr::Case case_) noexcept {
  TRY_DEF(scrutinee, parse_expr(ctx, std::move(*case_.scrutinee)));

  move_only_vector<expr::Choice> choices;
  for (auto &[pattern, result] : case_.choices) {
    TRY_DEF(parsed_pattern_with_names, patterny::parse_pattern(ctx, std::move(pattern)));
    auto [names, parsed_pattern] = *std::move(parsed_pattern_with_names);

    Context new_ctx{
        .ts = ctx.ts,
        .tags = ctx.tags,
        .scope = scope::Scope{std::move(names), &ctx.scope},
        .type_binding_depth = ctx.type_binding_depth,
    };

    TRY_DEF(parsed_result, parse_expr(new_ctx, std::move(*result)));
    choices.push_back({
        .pattern = std::move(parsed_pattern),
        .result = *std::move(parsed_result),
    });
  }

  return expr::Case{
      .scrutinee = std::make_unique<expr::Expr>(*std::move(scrutinee)),
      .choices = std::move(choices),
  };
}

std::expected<expr::TaggedValue, ParseError>
parse_expr(Context &ctx, ast::expr::TaggedValue tagged_value) noexcept {
  TRY_DEF(value, parse_expr(ctx, std::move(*tagged_value.value)));
  return expr::TaggedValue{
      .tag_id = ctx.tags.get_tag(tagged_value.tag),
      .value = std::make_unique<expr::Expr>(*std::move(value)),
  };
}

std::expected<expr::Pack, ParseError> parse_expr(Context &ctx, ast::expr::Pack pack) noexcept {
  move_only_vector<expr::TaggedValue> tagged_values;
  for (auto &tagged_value : pack.tagged_values) {
    TRY_DEF(value, parse_expr(ctx, std::move(*tagged_value.value)));
    tagged_values.push_back({
        .tag_id = ctx.tags.get_tag(tagged_value.tag),
        .value = std::make_unique<expr::Expr>(*std::move(value)),
    });
  }
  return expr::Pack{.tagged_values = std::move(tagged_values)};
}

std::expected<expr::Lambda, ParseError> parse_expr(Context &ctx,
                                                   ast::expr::Lambda lambda) noexcept {
  TRY_DEF(parsed_binding, parse_binding(ctx, std::move(lambda.binding)));
  auto binding = std::make_unique<entity::Binding>(*std::move(parsed_binding));
  auto new_ctx = ctx.with_binding(*binding);
  TRY_DEF(body, parse_expr(new_ctx, std::move(*lambda.body)));
  return expr::Lambda{
      // FIX: Unfinished.
      .captures = {},
      .binding = std::move(binding),
      .body = std::make_unique<expr::Expr>(*std::move(body)),
  };
}

std::expected<expr::TVLambda, ParseError> parse_expr(Context &ctx,
                                                     ast::expr::TVLambda tv_lambda) noexcept {
  auto new_ctx = ctx.with_type_binding(tv_lambda.type_binding);
  TRY_DEF(body, parse_expr(new_ctx, std::move(*tv_lambda.body)));
  return expr::TVLambda{.body = std::make_unique<expr::Expr>(*std::move(body))};
}

std::expected<expr::Expr, ParseError> parse_expr(Context &ctx,
                                                 ast::expr::ValueOrBindingReference ref) noexcept {
  auto scope_entry = ctx.scope.lookup(ref.name);
  if (not scope_entry) {
    todo();
  }

  if (auto *value = std::get_if<scope::Value>(&*scope_entry)) {
    return expr::ValueReference{.value_id = value->value_id};
  }
  if (auto *binding = std::get_if<scope::Binding>(&*scope_entry)) {
    return expr::BindingReference{.binding = binding->binding};
  }

  todo();
}

std::expected<expr::Expr, ParseError> parse_expr(Context &ctx, ast::expr::Expr expr) noexcept {
  auto visitor = [&](auto an_expr) -> std::expected<expr::Expr, ParseError> {
    return parse_expr(ctx, std::move(an_expr));
  };
  return std::visit(visitor, std::move(expr));
}

} // namespace expry

namespace entityy {

std::expected<entity::TypeDefinition, ParseError>
parse_type_definition(Context &ctx, ast::entity::TypeDefinition form) noexcept {
  move_only_vector<entity::TypeBinding> type_bindings;
  for (auto &[name, kind] : form.type_bindings) {
    type_bindings.push_back({
        .name = static_cast<std::string>(name),
        .kind = std::move(kind),
    });
  }

  move_only_vector<std::unique_ptr<Context>> ctxs;
  for (auto it = type_bindings.rbegin(); it != type_bindings.rend(); ++it) {
    auto &prev = ctxs.empty() ? ctx : *ctxs.back();
    ctxs.push_back(std::make_unique<Context>(prev.with_type_binding(it->name)));
  }

  auto &prev = ctxs.empty() ? ctx : *ctxs.back();
  TRY_DEF(type_id, typey::parse_type(prev, std::move(form.type)));
  return entity::TypeDefinition{
      .name = static_cast<std::string>(form.name),
      .type_bindings = std::move(type_bindings),
      .type_id = *type_id,
  };
}

std::expected<entity::ValueDefinition<expr::Expr>, ParseError>
parse_value_definition(Context &ctx, ast::entity::ValueDefinition def) noexcept {
  std::optional<id::TypeId> type_id;
  if (def.type) {
    TRY_DEF(parsed_type_id, typey::parse_type(ctx, *std::move(def.type)));
    type_id = *parsed_type_id;
  }

  ast::expr::Expr value = std::move(def.value);
  for (auto it = def.bindings.rbegin(); it != def.bindings.rend(); ++it) {
    auto &binding = *it;
    value = ast::expr::Lambda{
        .binding = std::move(binding),
        .body = std::make_unique<ast::expr::Expr>(std::move(value)),
    };
  }
  for (auto it = def.type_bindings.rbegin(); it != def.type_bindings.rend(); ++it) {
    auto &type_binding = *it;
    value = ast::expr::TVLambda{
        .type_binding = type_binding,
        .body = std::make_unique<ast::expr::Expr>(std::move(value)),
    };
  }

  TRY_DEF(expr, expry::parse_expr(ctx, std::move(value)));
  return entity::ValueDefinition<expr::Expr>{
      .type_id = type_id,
      .name = static_cast<std::string>(def.name),
      .value = *std::move(expr),
  };
}

} // namespace entityy

} // namespace

export struct ResolvedAST {
  type_storage::TypeStorage ts;
  move_only_vector<tag::Tag> tags;
  move_only_vector<entity::TypeDefinition> forms;
  move_only_vector<entity::ValueDefinition<expr::Expr>> values;
};

export std::expected<ResolvedAST, raw_parser::ParseError> parse(std::string_view view) noexcept {
  TRY_DEF(entities_result, raw_parser::parse(view));
  auto &entities = *entities_result;

  std::unordered_map<std::string_view, scope::Entry> names;
  std::size_t form_count = 0;
  std::size_t value_count = 0;

  for (auto &entity : entities) {
    struct Visitor {
      void operator()(ast::entity::TypeDefinition const &form) {
        auto [_, did_insert] = names.insert({
            form.name,
            scope::TypeDefinition{.form_id = id::FormId{form_count++}},
        });
        if (not did_insert) {
          todo();
        }
      }
      void operator()(ast::entity::ValueDefinition const &def) {
        auto [_, did_insert] = names.insert({
            def.name,
            scope::Value{.value_id = id::ValueId{value_count++}},
        });
        if (not did_insert) {
          todo();
        }
      }

      std::size_t &form_count;
      std::size_t &value_count;
      std::unordered_map<std::string_view, scope::Entry> &names;
    };

    std::visit(Visitor{form_count, value_count, names}, entity);
  }

  type_storage::TypeStorage ts;
  tag_storage::TagStorage tags;
  Context ctx{ts, tags, scope::Scope{std::move(names), nullptr}, 0};

  move_only_vector<entity::TypeDefinition> forms;
  move_only_vector<entity::ValueDefinition<expr::Expr>> values;
  forms.reserve(form_count);
  values.reserve(value_count);

  for (auto &entity : entities) {
    struct Visitor {
      void operator()(ast::entity::TypeDefinition form) {
        auto result = entityy::parse_type_definition(ctx, std::move(form));
        if (not result) {
          todo();
        }
        forms.push_back(*std::move(result));
      }
      void operator()(ast::entity::ValueDefinition def) {
        auto result = entityy::parse_value_definition(ctx, std::move(def));
        if (not result) {
          todo();
        }
        values.push_back(*std::move(result));
      }

      move_only_vector<entity::TypeDefinition> &forms;
      move_only_vector<entity::ValueDefinition<expr::Expr>> &values;
      Context &ctx;
    };

    std::visit(Visitor{forms, values, ctx}, std::move(entity));
  }

  return ResolvedAST{
      .ts = std::move(ts),
      .tags = std::move(tags).finalize(),
      .forms = std::move(forms),
      .values = std::move(values),
  };
}

} // namespace parser
