module;

#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module typechecker;

import constraint;
import entity;
import expr;
import formatter;
import id;
import move_only_vector;
import tag;
import type;
import type_storage;
import typed_expr;

namespace {

struct Env {
  void memoize(id::ValueId value_id, id::TypeId type_id) {
    auto [_, did_insert] = type_of_value.insert({value_id, type_id});
    assert(did_insert);
  }

  std::unordered_map<id::ValueId, id::TypeId, std::hash<id::Id<id::Domain::value>>> type_of_value;
};

struct Context {
  type_storage::TypeStorage &ts;
  constraint::Solver &solver;
  Env &env;
};

typed_expr::Expr get_type(Context &ctx, expr::Expr expr) noexcept {
  struct Visitor {
    typed_expr::Expr operator()(expr::Application call) {
      auto function = get_type(ctx, std::move(*call.function));
      auto argument = get_type(ctx, std::move(*call.argument));

      // Variable generation can be optimized here.
      // If function.type_id() is an arrow, reuse it.
      // If function.type_id() is a variable, merge it with an invented arrow.
      id::TypeId const from_id = ctx.ts.make_variable();
      id::TypeId const to_id = ctx.ts.make_variable();
      auto invented_function_type_id = ctx.ts.store_new(type::Arrow{from_id, to_id});
      ctx.solver.add_constraint(constraint::SubtypeOf{
          function.type_id(),
          invented_function_type_id,
      });
      auto invented_function = typed_expr::Conversion{
          {invented_function_type_id},
          std::make_unique<typed_expr::Expr>(std::move(function)),
      };

      ctx.solver.add_constraint(constraint::SubtypeOf{
          argument.type_id(),
          from_id,
      });
      return typed_expr::Application{
          {to_id},
          std::make_unique<typed_expr::Expr>(std::move(invented_function)),
          std::make_unique<typed_expr::Expr>(typed_expr::Conversion{
              {from_id},
              std::make_unique<typed_expr::Expr>(std::move(argument)),
          }),
      };
    }
    typed_expr::Expr operator()(expr::Case case_) {
      auto scrutinee = get_type(ctx, std::move(*case_.scrutinee));

      move_only_vector<typed_expr::Choice> choices;
      auto const type_id = ctx.ts.make_variable();
      for (auto &[pattern, arm] : case_.choices) {
        auto typed_arm = get_type(ctx, std::move(arm));

        ctx.solver.add_constraint(constraint::SubtypeOf{
            typed_arm.type_id(),
            type_id,
        });

        choices.push_back({
            .pattern = std::move(pattern),
            .arm = std::move(typed_arm),
        });
      }

      return typed_expr::Case{
          {type_id},
          std::make_unique<typed_expr::Expr>(std::move(scrutinee)),
          std::move(choices),
      };
    }
    typed_expr::Expr operator()(expr::TaggedValue v) {
      auto value = get_type(ctx, std::move(*v.value));
      auto const type_id = ctx.ts.store(type::Union{{type::Element{
          .tag_id = v.tag_id,
          .type_id = value.type_id(),
      }}});
      return typed_expr::TaggedValue{
          {type_id},
          v.tag_id,
          std::make_unique<typed_expr::Expr>(std::move(value)),
      };
    }
    typed_expr::Expr operator()(expr::Pack pack) {
      move_only_vector<type::Element> elements;
      move_only_vector<typed_expr::TaggedValue> tagged_values;
      for (auto &[tag_id, value] : pack.tagged_values) {
        auto typed_value = get_type(ctx, std::move(*value));

        elements.push_back({
            .tag_id = tag_id,
            .type_id = typed_value.type_id(),
        });
        tagged_values.push_back({
            {typed_value.type_id()},
            tag_id,
            std::make_unique<typed_expr::Expr>(std::move(typed_value)),
        });
      }

      auto const type_id = ctx.ts.store(type::Struct{std::move(elements)});
      return typed_expr::Pack{{type_id}, std::move(tagged_values)};
    }
    typed_expr::Expr operator()(expr::Lambda lambda) {
      auto body = get_type(ctx, std::move(*lambda.body));
      id::TypeId type_id;
      // Optimization: The tree is walked once, so this is the first time we encounter this binding.
      // If its type is a variable, then nothing could've mentioned it at this point,
      // so there are no types containing it.
      if (std::holds_alternative<type::Variable>(ctx.ts.read(lambda.binding->type_id))) {
        type_id = ctx.ts.store_new(type::Arrow{lambda.binding->type_id, body.type_id()});
      } else {
        type_id = ctx.ts.store(type::Arrow{lambda.binding->type_id, body.type_id()});
      }
      return typed_expr::Lambda{
          {type_id},
          std::move(lambda.captures),
          std::move(lambda.binding),
          std::make_unique<typed_expr::Expr>(std::move(body)),
      };
    }
    typed_expr::Expr operator()(expr::TVLambda tv_lambda) {
      auto body = get_type(ctx, std::move(*tv_lambda.body));
      auto const type_id = ctx.ts.store(type::ForAll{body.type_id()});
      return typed_expr::TVLambda{{type_id}, std::make_unique<typed_expr::Expr>(std::move(body))};
    }
    typed_expr::Expr operator()(expr::ValueReference value_ref) {
      auto const type_id = ctx.env.type_of_value.at(value_ref.value_id);
      return typed_expr::ValueReference{{type_id}, value_ref.value_id};
    }
    typed_expr::Expr operator()(expr::BindingReference binding_ref) {
      return typed_expr::BindingReference{
          {binding_ref.binding.get().type_id},
          binding_ref.binding,
      };
    }

    Context &ctx;
  };
  return std::visit(Visitor{ctx}, std::move(expr));
}

entity::ValueDefinition<typed_expr::Expr>
typecheck_value(Context &ctx, entity::ValueDefinition<expr::Expr> def) noexcept {
  auto value = get_type(ctx, std::move(def.value));
  if (def.type_id) {
    ctx.solver.add_constraint(constraint::SubtypeOf{value.type_id(), *def.type_id});
    return entity::ValueDefinition<typed_expr::Expr>{
        .type_id = def.type_id,
        .name = std::move(def.name),
        .value =
            typed_expr::Conversion{
                {*def.type_id},
                std::make_unique<typed_expr::Expr>(std::move(value)),
            },
    };
  } else {
    return entity::ValueDefinition<typed_expr::Expr>{
        .type_id = value.type_id(),
        .name = std::move(def.name),
        .value = std::move(value),
    };
  }
}

} // namespace

namespace typechecker {

export move_only_vector<entity::ValueDefinition<typed_expr::Expr>>
typecheck(type_storage::TypeStorage &ts, move_only_vector<tag::Tag> const &tags,
          move_only_vector<entity::TypeDefinition> const &forms,
          move_only_vector<entity::ValueDefinition<expr::Expr>> values) noexcept {
  Env env;
  move_only_vector<entity::ValueDefinition<typed_expr::Expr>> typed_entities;
  for (std::size_t i = 0; i < values.size(); ++i) {
    auto &value = values[i];

    constraint::Solver solver;

    Context ctx{ts, solver, env};
    auto var_id = ts.make_variable();
    env.memoize(id::ValueId{{.value = i}}, var_id);
    auto typed_value = typecheck_value(ctx, std::move(value));
    solver.add_constraint(constraint::SubtypeOf{var_id, *typed_value.type_id});
    solver.add_constraint(constraint::SubtypeOf{*typed_value.type_id, var_id});

    std::cout << typed_value.name << " : "
              << formatter::type_name({ts, forms, tags}, *typed_value.type_id) << '\n';
    std::cout << "CONSTRAINTS:\n";
    solver.solve(
        std::cout,
        [&](std::ostream &os, id::TypeId id) { os << formatter::type_name({ts, forms, tags}, id); },
        forms, ts);
    std::cout << "DONE.\n";
    std::cout << typed_value.name << " : "
              << formatter::type_name({ts, forms, tags}, *typed_value.type_id) << '\n';

    typed_entities.push_back(std::move(typed_value));
  }

  return typed_entities;
}

} // namespace typechecker
