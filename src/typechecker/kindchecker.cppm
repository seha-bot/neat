module;

#include <cassert>
#include <cstddef>
#include <expected>
#include <functional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "common/try.hpp"

export module kindchecker;

import constraint;
import entity;
import expr;
import formatter;
import id;
import move_only_vector;
import tag;
import todo;
import type;
import type_storage;
import typed_expr;

namespace {

struct Env {
  void memoize(id::FormId form_id, id::KindId kind_id) {
    auto [_, did_insert] = kind_of_form.insert({form_id, kind_id});
    assert(did_insert);
  }

  std::unordered_map<id::FormId, id::KindId, std::hash<id::Id<id::Domain::form>>> kind_of_form;
};

struct Context {
  type_storage::TypeStorage &ts;
  move_only_vector<id::KindId> kind_stack;
  Env env;
};

struct Error {};

auto expect_type(type_storage::TypeStorage const &ts) noexcept {
  return [&ts](id::KindId kind_id) -> std::expected<void, Error> {
    if (not std::holds_alternative<kind::Type>(ts.read_kind(kind_id))) {
      todo();
    }
    return {};
  };
}

std::expected<id::KindId, Error> check(Context &ctx, id::TypeId const &type) noexcept {
  struct Visitor {
    std::expected<id::KindId, Error> operator()(type::Arrow const &arr) {
      TRY(check(ctx, arr.from_id).and_then(expect_type(ctx.ts)));
      TRY(check(ctx, arr.to_id).and_then(expect_type(ctx.ts)));
      return ctx.ts.store_kind(kind::Type{});
    }
    std::expected<id::KindId, Error> operator()(type::ForAll const &for_all) {
      ctx.kind_stack.push_back(for_all.binding_kind_id);
      TRY_DEF(kind, check(ctx, for_all.type_id));
      ctx.kind_stack.pop_back();
      return kind;
    }
    std::expected<id::KindId, Error> operator()(type::DeBruijnIndex const &index) {
      return ctx.kind_stack.rbegin()[static_cast<std::ptrdiff_t>(index.value)];
    }
    std::expected<id::KindId, Error> operator()(type::Union const &union_) {
      for (auto &[_, type_id] : union_.elements) {
        TRY(check(ctx, type_id).and_then(expect_type(ctx.ts)));
      }
      return ctx.ts.store_kind(kind::Type{});
    }
    std::expected<id::KindId, Error> operator()(type::Struct const &struct_) {
      for (auto &[_, type_id] : struct_.elements) {
        TRY(check(ctx, type_id).and_then(expect_type(ctx.ts)));
      }
      return ctx.ts.store_kind(kind::Type{});
    }
    std::expected<id::KindId, Error> operator()(type::TTLambda const &) {
      std::unreachable();
      // ctx.kind_stack.push_back(tt_lambda.binding_kind_id);
      // TRY_DEF(kind_id, check(ctx, tt_lambda.type_id));
      // ctx.kind_stack.pop_back();
      // return ctx.ts.store_kind(kind::Arrow{
      //     .from_id = tt_lambda.binding_kind_id,
      //     .to_id = *kind_id,
      // });
    }
    std::expected<id::KindId, Error> operator()(type::Application const &app) {
      TRY_DEF(function_kind_id, check(ctx, app.function_id));
      TRY_DEF(argument_kind_id, check(ctx, app.argument_id));
      auto *arrow = std::get_if<kind::Arrow>(&ctx.ts.read_kind(*function_kind_id));
      if (not arrow) {
        todo();
      }
      if (arrow->from_id != argument_kind_id) {
        todo();
      }
      return arrow->to_id;
    }
    std::expected<id::KindId, Error> operator()(type::Variable const &var) { return var.kind_id; }
    std::expected<id::KindId, Error> operator()(type::RigidVariable const &var) {
      return var.kind_id;
    }
    std::expected<id::KindId, Error> operator()(type::NamedTypeReference const &ref) {
      return ctx.env.kind_of_form.at(ref.form_id);
    }

    Context &ctx;
  };

  return std::visit(Visitor{ctx}, ctx.ts.read(type));
}

} // namespace

namespace kindchecker {

export void check(type_storage::TypeStorage &ts,
                  move_only_vector<entity::TypeDefinition> const &forms) noexcept {
  Context ctx{ts, {}, {}};

  for (std::size_t i = 0; i < forms.size(); ++i) {
    id::FormId const form_id{i};

    kind::Kind kind = kind::Type{};
    auto type_id = forms[i].type_id;
    while (auto *tt_lambda = std::get_if<type::TTLambda>(&ts.read(type_id))) {
      ctx.kind_stack.push_back(tt_lambda->binding_kind_id);
      kind = kind::Arrow{
          .from_id = tt_lambda->binding_kind_id,
          .to_id = ts.store_kind(kind),
      };
      type_id = tt_lambda->type_id;
    }

    ctx.env.memoize(form_id, ts.store_kind(kind));
    if (not ::check(ctx, type_id).and_then(expect_type(ctx.ts))) {
      todo();
    }
  }
}

} // namespace kindchecker
