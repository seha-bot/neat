module;

#include <cstddef>
#include <ostream>
#include <string>
#include <variant>

export module formatter;

import ast;
import entity;
import expr;
import id;
import move_only_vector;
import tag;
import todo;
import type;
import type_storage;

export namespace formatter {

struct Context {
  type_storage::TypeStorage const &ts;
  move_only_vector<entity::TypeDefinition> const &forms;
  move_only_vector<entity::ValueDefinition<expr::Expr>> const &values;
  move_only_vector<tag::Tag> const &tags;
};

} // namespace formatter

namespace {

void format_pattern(std::ostream &os, formatter::Context ctx, expr::pattern::Pattern const &pat) {
  struct Visitor {
    void operator()(expr::pattern::TaggedValue const &t) {
      os << ctx.tags[t.tag_id.value].name << ' ';
      format_pattern(os, ctx, *t.value);
    }
    void operator()(expr::pattern::Pack const &p) {
      os << '{';
      for (auto &t : p.tagged_values) {
        os << ctx.tags[t.tag_id.value].name << " = ";
        format_pattern(os, ctx, *t.value);
        os << ',';
      }
      os << '}';
    }
    void operator()(expr::pattern::Binding const &b) { os << b.binding->name; }

    std::ostream &os;
    formatter::Context ctx;
  };
  std::visit(Visitor{os, ctx}, pat);
}

} // namespace

export namespace formatter {

struct TypeContext {
  type_storage::TypeStorage const &ts;
  move_only_vector<entity::TypeDefinition> const &forms;
  move_only_vector<tag::Tag> const &tags;
};

std::string type_name(TypeContext ctx, id::TypeId t) {
  struct Visitor {
    std::string operator()(type::Arrow const &b) {
      return "(" + type_name(ctx, b.from_id) + ") -> " + type_name(ctx, b.to_id);
    }
    std::string operator()(type::ForAll const &c) { return "∀ " + type_name(ctx, c.type_id); }
    std::string operator()(type::DeBruijnIndex const &d) { return std::to_string(d.value); }
    std::string operator()(type::Union const &v) {
      if (v.elements.empty()) {
        return "NEVER";
      }

      std::string str;
      for (auto &[tag_id, type_id] : v.elements) {
        if (str.empty()) {
          str += ":";
        } else {
          str += " | :";
        }
        str += ctx.tags[tag_id.value].name + ' ' + type_name(ctx, type_id);
      }
      return str;
    }
    std::string operator()(type::Struct const &s) {
      if (s.elements.empty()) {
        return "{}";
      }
      std::string str = "{";
      for (auto &[tag_id, type_id] : s.elements) {
        str += " " + ctx.tags[tag_id.value].name + " :: " + type_name(ctx, type_id) + ",";
      }
      return str + " }";
    }
    std::string operator()(type::TTLambda const &tt) { return "Π " + type_name(ctx, tt.type_id); }
    std::string operator()(type::Application const &app) {
      return type_name(ctx, app.function_id) + " (" + type_name(ctx, app.argument_id) + ")";
    }
    std::string operator()(type::Variable const &) {
      return "#" + std::to_string(ctx.ts.m_rep.representative(t).value);
    }
    std::string operator()(type::NamedTypeReference const &a) {
      return ctx.forms[a.form_id.value].name;
    }

    TypeContext ctx;
    id::TypeId t;
  };
  return std::visit(Visitor{ctx, t}, ctx.ts.read(t));
}

void format_expr(std::ostream &os, Context ctx, std::size_t depth, expr::Expr const &expr) {
  struct Visitor {
    void operator()(expr::Application const &app) {
      format_expr(os, ctx, 0, *app.function);
      os << " (";
      format_expr(os, ctx, 0, *app.argument);
      os << ')';
    }
    void operator()(expr::Case const &c) {
      os << "case ";
      format_expr(os, ctx, 0, *c.scrutinee);
      os << " of {";
      for (auto &[pattern, result] : c.choices) {
        os << ' ';
        format_pattern(os, ctx, pattern);
        os << " -> ";
        format_expr(os, ctx, 0, result);
        os << ',';
      }
      os << " }";
    }
    void operator()(expr::TaggedValue const &v) {
      os << ':' << ctx.tags[v.tag_id.value].name << ' ';
      format_expr(os, ctx, 0, *v.value);
    }
    void operator()(expr::Pack const &p) {
      os << '{';
      for (auto &val : p.tagged_values) {
        os << ctx.tags[val.tag_id.value].name << " = ";
        format_expr(os, ctx, 0, *val.value);
        os << ',';
      }
      os << '}';
    }
    void operator()(expr::Lambda const &l) {
      os << '|' << l.binding->name
         << " :: " << type_name({ctx.ts, ctx.forms, ctx.tags}, l.binding->type_id) << "| ";
      format_expr(os, ctx, 0, *l.body);
    }
    void operator()(expr::TVLambda const &l) {
      os << "Λ ";
      format_expr(os, ctx, 0, *l.body);
    }
    void operator()(expr::ValueReference const &v) { os << ctx.values[v.value_id.value].name; }
    void operator()(expr::BindingReference const &b) { os << b.binding.get().name; }

    std::ostream &os;
    Context ctx;
    std::size_t depth;
  };
  os << std::string(depth, ' ');
  std::visit(Visitor{os, ctx, depth}, expr);
}

void format_kind(std::ostream &os, ast::kind::Kind const &) { os << '?'; }

void format_form(std::ostream &os, TypeContext ctx, entity::TypeDefinition const &form) {
  os << "form " << form.name;
  // for (auto &[name, kind] : form.type_bindings) {
  //   os << " (" << name << ' ';
  //   format_kind(os, kind);
  //   os << ')';
  // }
  os << " = " << type_name(ctx, form.type_id);
}

void format_value(std::ostream &os, Context ctx, entity::ValueDefinition<expr::Expr> const &value) {
  if (value.type_id) {
    os << "dec " << value.name << " :: " << type_name({ctx.ts, ctx.forms, ctx.tags}, *value.type_id)
       << '\n';
  }
  os << "def " << value.name << " = ";
  format_expr(os, ctx, 0, value.value);
}

} // namespace formatter
