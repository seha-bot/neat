module;

#include <cstddef>
#include <format>
#include <string>
#include <variant>

export module formatter;

import ast;
import entity;
import expr;
import id;
import move_only_vector;
import tag;
import type;
import type_storage;

export namespace formatter {

struct TypeContext;
struct PatternContext;
struct ExprContext;

struct Context {
  TypeContext with_type(id::TypeId type_id) const;

  type_storage::TypeStorage const &ts;
  move_only_vector<entity::TypeDefinition> const &forms;
  move_only_vector<tag::Tag> const &tags;
};

struct ExtendedContext : Context {
  PatternContext with_pattern(expr::pattern::Pattern const &pattern) const;
  ExprContext with_expr(expr::Expr const &expr) const;

  move_only_vector<entity::ValueDefinition<expr::Expr>> const &values;
};

struct TypeContext : Context {
  id::TypeId type_id;
};

struct PatternContext : ExtendedContext {
  expr::pattern::Pattern const &pattern;
};

struct ExprContext : ExtendedContext {
  expr::Expr const &expr;
};

TypeContext Context::with_type(id::TypeId type_id) const { return {*this, type_id}; }
PatternContext ExtendedContext::with_pattern(expr::pattern::Pattern const &pattern) const {
  return {*this, pattern};
}
ExprContext ExtendedContext::with_expr(expr::Expr const &expr) const { return {*this, expr}; }

} // namespace formatter

export template <> struct std::formatter<::formatter::TypeContext, char> {
  template <class ParseContext> constexpr ParseContext::iterator parse(ParseContext &ctx) {
    return ctx.begin();
  }

  template <class FmtContext>
  FmtContext::iterator format(::formatter::TypeContext tctx, FmtContext &ctx) const {
    struct Visitor {
      FmtContext::iterator operator()(type::Arrow const &a) {
        auto &t = tctx.ts.read(a.from_id);
        if (std::holds_alternative<type::ForAll>(t) or std::holds_alternative<type::Union>(t) or
            std::holds_alternative<type::TTLambda>(t) or
            std::holds_alternative<type::TTLambda>(t)) {
          return std::format_to(ctx.out(), "({}) -> {}", tctx.with_type(a.from_id),
                                tctx.with_type(a.to_id));
        } else {
          return std::format_to(ctx.out(), "{} -> {}", tctx.with_type(a.from_id),
                                tctx.with_type(a.to_id));
        }
      }
      FmtContext::iterator operator()(type::ForAll const &f) {
        return std::format_to(ctx.out(), "∀ {}", tctx.with_type(f.type_id));
      }
      FmtContext::iterator operator()(type::DeBruijnIndex const &d) {
        return std::format_to(ctx.out(), "{}", d.value);
      }
      FmtContext::iterator operator()(type::Union const &u) {
        if (u.elements.empty()) {
          return std::format_to(ctx.out(), "NEVER");
        }

        auto it = ctx.out();
        for (std::size_t i = 0; i < u.elements.size(); ++i) {
          auto &[tag_id, type_id] = u.elements[i];
          if (i == 0) {
            it = std::format_to(it, ":");
          } else {
            it = std::format_to(it, " | :");
          }
          it = std::format_to(it, "{} {}", tctx.tags[tag_id.value].name, tctx.with_type(type_id));
        }
        return it;
      }
      FmtContext::iterator operator()(type::Struct const &s) {
        if (s.elements.empty()) {
          return std::format_to(ctx.out(), "{{}}");
        }

        auto it = std::format_to(ctx.out(), "{{");
        for (auto &[tag_id, type_id] : s.elements) {
          it = std::format_to(ctx.out(), " {} :: {},", tctx.tags[tag_id.value].name,
                              tctx.with_type(type_id));
        }
        return std::format_to(it, " }}");
      }
      FmtContext::iterator operator()(type::TTLambda const &t) {
        return std::format_to(ctx.out(), "Π {}", tctx.with_type(t.type_id));
      }
      FmtContext::iterator operator()(type::Application const &a) {
        auto it = ctx.out();
        auto format_one = [&](bool right, id::TypeId id) {
          auto &t = tctx.ts.read(id);
          if (std::holds_alternative<type::Arrow>(t) or std::holds_alternative<type::ForAll>(t) or
              std::holds_alternative<type::Union>(t) or std::holds_alternative<type::TTLambda>(t) or
              std::holds_alternative<type::TTLambda>(t) or
              (std::holds_alternative<type::Application>(t) and right)) {
            it = std::format_to(it, "({})", tctx.with_type(id));
          } else {
            it = std::format_to(it, "{}", tctx.with_type(id));
          }
        };
        format_one(false, a.function_id);
        it = std::format_to(it, " ");
        format_one(true, a.argument_id);
        return it;
      }
      FmtContext::iterator operator()(type::Variable const &) {
        return std::format_to(ctx.out(), "#{}", tctx.ts.m_rep.representative(tctx.type_id).value);
      }
      FmtContext::iterator operator()(type::RigidVariable const &) {
        return std::format_to(ctx.out(), "!{}", tctx.ts.m_rep.representative(tctx.type_id).value);
      }
      FmtContext::iterator operator()(type::NamedTypeReference const &r) {
        return std::format_to(ctx.out(), "{}", tctx.forms[r.form_id.value].name);
      }

      ::formatter::TypeContext tctx;
      FmtContext &ctx;
    };
    return std::visit(Visitor{tctx, ctx}, tctx.ts.read(tctx.type_id));
  }
};

export template <> struct std::formatter<::formatter::PatternContext, char> {
  template <class ParseContext> constexpr ParseContext::iterator parse(ParseContext &ctx) {
    return ctx.begin();
  }

  template <class FmtContext>
  FmtContext::iterator format(::formatter::PatternContext pctx, FmtContext &ctx) const {
    struct Visitor {
      FmtContext::iterator operator()(expr::pattern::TaggedValue const &t) {
        if (std::holds_alternative<expr::pattern::TaggedValue>(*t.value)) {
          return std::format_to(ctx.out(), "{} ({})", pctx.tags[t.tag_id.value].name,
                                pctx.with_pattern(*t.value));
        }
        return std::format_to(ctx.out(), "{} {}", pctx.tags[t.tag_id.value].name,
                              pctx.with_pattern(*t.value));
      }
      FmtContext::iterator operator()(expr::pattern::Pack const &p) {
        if (p.tagged_values.empty()) {
          return std::format_to(ctx.out(), "{{}}");
        }

        auto it = std::format_to(ctx.out(), "{{");
        for (auto &[tag_id, value] : p.tagged_values) {
          it = std::format_to(ctx.out(), " {} = {},", pctx.tags[tag_id.value].name,
                              pctx.with_pattern(*value));
        }
        return std::format_to(it, " }}");
      }
      FmtContext::iterator operator()(expr::pattern::Binding const &b) {
        return std::format_to(ctx.out(), "{}", b.binding->name);
      }

      ::formatter::PatternContext pctx;
      FmtContext &ctx;
    };
    return std::visit(Visitor{pctx, ctx}, pctx.pattern);
  }
};

export template <> struct std::formatter<::formatter::ExprContext, char> {
  template <class ParseContext> constexpr ParseContext::iterator parse(ParseContext &ctx) {
    return ctx.begin();
  }

  template <class FmtContext>
  FmtContext::iterator format(::formatter::ExprContext ectx, FmtContext &ctx) const {
    struct Visitor {
      FmtContext::iterator operator()(expr::Application const &app) {
        auto it = ctx.out();
        auto format_one = [&](bool right, expr::Expr const &e) {
          if (std::holds_alternative<expr::TaggedValue>(e) or
              std::holds_alternative<expr::Lambda>(e) or
              std::holds_alternative<expr::TVLambda>(e) or
              (std::holds_alternative<expr::Application>(e) and right)) {
            it = std::format_to(it, "({})", ectx.with_expr(e));
          } else {
            it = std::format_to(it, "{}", ectx.with_expr(e));
          }
        };
        format_one(false, *app.function);
        it = std::format_to(it, " ");
        format_one(true, *app.argument);
        return it;
      }
      FmtContext::iterator operator()(expr::Case const &c) {
        auto it = std::format_to(ctx.out(), "case {} of {{", ectx.with_expr(*c.scrutinee));

        if (c.choices.empty()) {
          return std::format_to(it, "}}");
        }

        for (auto &[pattern, result] : c.choices) {
          it = std::format_to(it, " {} -> {},", ectx.with_pattern(pattern), ectx.with_expr(result));
        }
        return std::format_to(it, " }}");
      }
      FmtContext::iterator operator()(expr::TaggedValue const &v) {
        if (std::holds_alternative<expr::Application>(*v.value) or
            std::holds_alternative<expr::TaggedValue>(*v.value)) {
          return std::format_to(ctx.out(), ":{} ({})", ectx.tags[v.tag_id.value].name,
                                ectx.with_expr(*v.value));
        }
        return std::format_to(ctx.out(), ":{} {}", ectx.tags[v.tag_id.value].name,
                              ectx.with_expr(*v.value));
      }
      FmtContext::iterator operator()(expr::Pack const &p) {
        if (p.tagged_values.empty()) {
          return std::format_to(ctx.out(), "{{}}");
        }

        auto it = std::format_to(ctx.out(), "{{");
        for (auto &[tag_id, value] : p.tagged_values) {
          it = std::format_to(ctx.out(), " {} = {},", ectx.tags[tag_id.value].name,
                              ectx.with_expr(*value));
        }
        return std::format_to(it, " }}");
      }
      FmtContext::iterator operator()(expr::Lambda const &l) {
        return std::format_to(ctx.out(), "|{} :: {}| {}", l.binding->name,
                              ectx.with_type(l.binding->type_id), ectx.with_expr(*l.body));
      }
      FmtContext::iterator operator()(expr::TVLambda const &tv) {
        return std::format_to(ctx.out(), "Λ {}", ectx.with_expr(*tv.body));
      }
      FmtContext::iterator operator()(expr::Instantiation const &i) {
        if (std::holds_alternative<expr::Application>(*i.function) or
            std::holds_alternative<expr::TaggedValue>(*i.function) or
            std::holds_alternative<expr::Lambda>(*i.function) or
            std::holds_alternative<expr::TVLambda>(*i.function)) {
          return std::format_to(ctx.out(), "({})[:{}:]", ectx.with_expr(*i.function),
                                ectx.with_type(i.argument_id));
        }
        return std::format_to(ctx.out(), "{}[:{}:]", ectx.with_expr(*i.function),
                              ectx.with_type(i.argument_id));
      }
      FmtContext::iterator operator()(expr::ValueReference const &v) {
        return std::format_to(ctx.out(), "{}", ectx.values[v.value_id.value].name);
      }
      FmtContext::iterator operator()(expr::BindingReference const &b) {
        return std::format_to(ctx.out(), "{}", b.binding.get().name);
      }

      ::formatter::ExprContext ectx;
      FmtContext &ctx;
    };
    return std::visit(Visitor{ectx, ctx}, ectx.expr);
  }
};

export namespace formatter {

std::string format_form(Context ctx, entity::TypeDefinition const &form) {
  return std::format("form {} = {}", form.name, ctx.with_type(form.type_id));
}

std::string format_value(ExtendedContext ctx,
                  entity::ValueDefinition<expr::Expr> const &value) {
  if (value.type_id) {
    return std::format("dec {} :: {}\n", value.name, ctx.with_type(*value.type_id));
  }
  return std::format("def {} = {}", value.name, ctx.with_expr(value.value));
}

} // namespace formatter
