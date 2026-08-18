module;

#include <cassert>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

#include "common/try.hpp"

export module raw_parser;

import ast;
import move_only_vector;
import token;

namespace raw_parser {

struct UnexpectedToken {
  friend std::ostream &operator<<(std::ostream &os, UnexpectedToken const &t) {
    os << "Unexpected token: \"" << t.got.view << "\" at " << t.got.range << ".\n";
    os << "Expected:";
    for (auto &type : t.expected) {
      os << " " << token::type_to_string(type);
    }
    os << '\n';
    return os;
  }

  std::vector<token::Type> expected;
  token::Token got;
};

export struct ParseError {
  ParseError(UnexpectedToken error) : m_errors{error} {}

  ParseError(std::vector<ParseError> errors) {
    for (auto &err : errors) {
      assert(not err.is_hard());
      m_errors.insert(m_errors.end(), std::move_iterator(err.m_errors.begin()),
                      std::move_iterator(err.m_errors.end()));
    }
  }

  friend std::ostream &operator<<(std::ostream &os, ParseError const &e) {
    for (auto &error : e.m_errors) {
      os << error;
    }
    return os;
  }

  bool is_hard() const { return m_is_hard; }
  void make_hard() { m_is_hard = true; }

private:
  std::vector<UnexpectedToken> m_errors;
  bool m_is_hard = false;
};

namespace {

struct Checkpoint {
  token::Checkpoint tokenizer;
  token::Token token;
};

struct Parser {
  Parser(token::Tokenizer &tokenizer) noexcept
      : m_tokenizer(tokenizer), m_token(tokenizer.next()) {}

  Parser(Parser const &) = delete;
  Parser(Parser &&) = default;
  Parser &operator=(Parser const &) = delete;
  Parser &operator=(Parser &&) = default;

  void eat() noexcept { m_token = m_tokenizer.get().next(); }

  template <token::Type... Types>
  std::expected<token::Token, UnexpectedToken> expected() const noexcept {
    if (((m_token.type != Types) and ...)) {
      return std::unexpected(UnexpectedToken{
          .expected = {Types...},
          .got = m_token,
      });
    }
    return m_token;
  }

  template <token::Type... Types> std::expected<token::Token, UnexpectedToken> expect() noexcept {
    TRY_DEF(token, expected<Types...>());
    eat();
    return token;
  }

  [[nodiscard]] Checkpoint checkpoint() const noexcept {
    return {
        .tokenizer = m_tokenizer.get().checkpoint(),
        .token = m_token,
    };
  }

  void restore(Checkpoint cp) noexcept {
    m_tokenizer.get().restore(cp.tokenizer);
    m_token = cp.token;
  }

private:
  std::reference_wrapper<token::Tokenizer> m_tokenizer;
  token::Token m_token;
};

ParseError perr(UnexpectedToken e) noexcept { return e; }

template <typename T>
std::expected<T, ParseError> cut(std::expected<T, ParseError> result) noexcept {
  return std::move(result).transform_error([](ParseError error) {
    error.make_hard();
    return error;
  });
}

template <typename T>
std::expected<T, ParseError> parse_any(Parser &parser, auto... parse_fns) noexcept {
  auto const cp = parser.checkpoint();

  std::optional<T> result;
  std::optional<ParseError> hard_error;
  std::vector<ParseError> errors;
  ([&] {
    auto x = parse_fns(parser);
    if (x) {
      result = *std::move(x);
      return false;
    }
    parser.restore(cp);

    if (x.error().is_hard()) {
      hard_error = std::move(x).error();
      return false;
    }
    errors.push_back(std::move(x).error());
    return true;
  }() and
   ...);

  if (result) {
    return *std::move(result);
  }
  if (hard_error) {
    return std::unexpected(*std::move(hard_error));
  }
  return std::unexpected(ParseError{std::move(errors)});
}

auto parenthesized(auto f) noexcept {
  return [f](Parser &parser) -> decltype(f(parser)) {
    TRY(parser.expect<token::Type::left_paren>());
    return cut([&] -> decltype(f(parser)) {
      TRY_DEF(x, f(parser));
      TRY(parser.expect<token::Type::right_paren>());
      return x;
    }());
  };
}

// arrow := kind_step "->" ! kind
// type := "*"
// kind_step := type | "(" ! kind ")"
// kind := arrow | kind_step
namespace kind {

std::expected<ast::kind::Kind, ParseError> parse_kind_step(Parser &parser) noexcept;
std::expected<ast::kind::Kind, ParseError> parse_kind(Parser &parser) noexcept;

std::expected<ast::kind::Arrow, ParseError> parse_arrow(Parser &parser) noexcept {
  TRY_DEF(from, parse_kind_step(parser));
  TRY(parser.expect<token::Type::arrow>());
  TRY_DEF(to, cut(parse_kind(parser)));
  return ast::kind::Arrow{
      .from = std::make_unique<ast::kind::Kind>(*std::move(from)),
      .to = std::make_unique<ast::kind::Kind>(*std::move(from)),
  };
}

std::expected<ast::kind::Type, ParseError> parse_type(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::star>());
  return ast::kind::Type{};
}

std::expected<ast::kind::Kind, ParseError> parse_kind_step(Parser &parser) noexcept {
  return parse_any<ast::kind::Kind>(parser, parse_type, parenthesized(parse_kind));
}

std::expected<ast::kind::Kind, ParseError> parse_kind(Parser &parser) noexcept {
  return parse_any<ast::kind::Kind>(parser, parse_arrow, parse_kind_step);
}

} // namespace kind

// arrow := app_type "->" ! arrow_type
// forall := "[" ! (id "::" kind | id) "]" arrow_type
// struct_step := id "::" type ("," struct_step | "}") | "}"
// struct := "{" ! struct_step
// This is left-associative.
// app := primary_type (primary_type)+
// ref := id
// union_element := ":" ! (id arrow_type | id)
// union := union_element "|" union | union_element
// primary_type := forall | struct | ref | "(" ! type ")"
// app_type := app | primary_type
// arrow_type := arrow | app_type
// type := union | arrow_type
namespace type {

std::expected<ast::type::Type, ParseError> parse_primary_type(Parser &parser) noexcept;
std::expected<ast::type::Type, ParseError> parse_app_type(Parser &parser) noexcept;
std::expected<ast::type::Type, ParseError> parse_arrow_type(Parser &parser) noexcept;
std::expected<ast::type::Type, ParseError> parse_type(Parser &parser) noexcept;

std::expected<ast::type::Arrow, ParseError> parse_arrow(Parser &parser) noexcept {
  TRY_DEF(from, parse_app_type(parser));
  TRY(parser.expect<token::Type::arrow>());
  TRY_DEF(to, cut(parse_arrow_type(parser)));
  return ast::type::Arrow{
      .from = std::make_unique<ast::type::Type>(*std::move(from)),
      .to = std::make_unique<ast::type::Type>(*std::move(to)),
  };
}

std::expected<ast::type::ForAll, ParseError> parse_for_all(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::left_bracket>());
  return cut([&] -> decltype(parse_for_all(parser)) {
    TRY_DEF(type_binding, parser.expect<token::Type::id>());
    ast::kind::Kind kind = ast::kind::Type{};
    if (parser.expect<token::Type::has_type>()) {
      TRY_DEF(parsed_kind, kind::parse_kind(parser));
      kind = *std::move(parsed_kind);
    }
    TRY(parser.expect<token::Type::right_bracket>());
    TRY_DEF(type, parse_arrow_type(parser));
    return ast::type::ForAll{
        .binding =
            ast::type::Binding{
                .name = type_binding->view,
                .kind = std::move(kind),
            },
        .type = std::make_unique<ast::type::Type>(*std::move(type)),
    };
  }());
}

// TODO: Identical to parse_pack from expr.
std::expected<ast::type::Struct, ParseError> parse_struct(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::left_brace>());
  return cut([&] -> decltype(parse_struct(parser)) {
    move_only_vector<ast::type::Element> elements;

    while (true) {
      TRY(parser.expected<token::Type::id, token::Type::right_brace>());
      if (parser.expect<token::Type::right_brace>()) {
        break;
      }

      TRY_DEF(tag, parser.expect<token::Type::id>());
      TRY(parser.expect<token::Type::has_type>());
      TRY_DEF(type, parse_type(parser));

      elements.push_back({
          .tag = tag->view,
          .type = *std::move(type),
      });

      TRY(parser.expected<token::Type::comma, token::Type::right_brace>());
      auto _ = parser.expect<token::Type::comma>();
    }

    return ast::type::Struct{.elements = std::move(elements)};
  }());
}

std::expected<ast::type::Application, ParseError> parse_application(Parser &parser) noexcept {
  TRY_DEF(function, parse_primary_type(parser));
  TRY_DEF(argument, parse_primary_type(parser));
  ast::type::Application app{
      .function = std::make_unique<ast::type::Type>(*std::move(function)),
      .argument = std::make_unique<ast::type::Type>(*std::move(argument)),
  };
  while (auto next_argument = parse_primary_type(parser)) {
    app = ast::type::Application{
        .function = std::make_unique<ast::type::Type>(std::move(app)),
        .argument = std::make_unique<ast::type::Type>(*std::move(next_argument)),
    };
  }
  return app;
}

std::expected<ast::type::NamedTypeOrTypeBindingReference, ParseError>
parse_named_type_reference(Parser &parser) noexcept {
  TRY_DEF(name, parser.expect<token::Type::id>());
  return ast::type::NamedTypeOrTypeBindingReference{.name = name->view};
}

std::expected<ast::type::Union, ParseError> parse_union(Parser &parser) noexcept {
  move_only_vector<ast::type::Element> elements;
  while (true) {
    TRY(parser.expect<token::Type::colon>());
    TRY_DEF(tag, cut(parser.expect<token::Type::id>().transform_error(perr)));
    if (auto type = parse_arrow_type(parser)) {
      elements.push_back({
          .tag = tag->view,
          .type = *std::move(type),
      });
    } else {
      elements.push_back({
          .tag = tag->view,
          .type = ast::type::Struct{},
      });
    }
    if (not parser.expect<token::Type::pipe>()) {
      break;
    }
  }
  return ast::type::Union{.elements = std::move(elements)};
}

std::expected<ast::type::Type, ParseError> parse_primary_type(Parser &parser) noexcept {
  return parse_any<ast::type::Type>(parser, parse_for_all, parse_struct, parse_named_type_reference,
                                    parenthesized(parse_type));
}

std::expected<ast::type::Type, ParseError> parse_app_type(Parser &parser) noexcept {
  return parse_any<ast::type::Type>(parser, parse_application, parse_primary_type);
}

std::expected<ast::type::Type, ParseError> parse_arrow_type(Parser &parser) noexcept {
  return parse_any<ast::type::Type>(parser, parse_arrow, parse_app_type);
}

std::expected<ast::type::Type, ParseError> parse_type(Parser &parser) noexcept {
  return parse_any<ast::type::Type>(parser, parse_union, parse_arrow_type);
}

} // namespace type

// tagged_value := ":" ! (id pattern | id)
// pack_step := id "=" pattern ("," pack_step | "}") | "}"
// pack := "{" ! pack_step
// binding := id
// pattern := tagged_value | pack | binding
namespace pattern {

std::expected<ast::pattern::Pattern, ParseError> parse_pattern(Parser &parser) noexcept;

std::expected<ast::pattern::TaggedValue, ParseError> parse_tagged_value(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::colon>());
  TRY_DEF(tag, cut(parser.expect<token::Type::id>().transform_error(perr)));
  if (auto value = parse_pattern(parser)) {
    return ast::pattern::TaggedValue{
        .tag = tag->view,
        .value = std::make_unique<ast::pattern::Pattern>(*std::move(value)),
    };
  } else {
    return ast::pattern::TaggedValue{
        .tag = tag->view,
        .value = std::make_unique<ast::pattern::Pattern>(ast::pattern::Pack{}),
    };
  }
}

// TODO: Identical to parse_pack from expr.
std::expected<ast::pattern::Pack, ParseError> parse_pack(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::left_brace>());

  return cut([&] -> decltype(parse_pack(parser)) {
    move_only_vector<ast::pattern::TaggedValue> tagged_values;

    while (true) {
      TRY(parser.expected<token::Type::id, token::Type::right_brace>());
      if (parser.expect<token::Type::right_brace>()) {
        break;
      }

      TRY_DEF(tag, parser.expect<token::Type::id>());
      TRY(parser.expect<token::Type::equal>());
      TRY_DEF(value, parse_pattern(parser));

      tagged_values.push_back({
          .tag = tag->view,
          .value = std::make_unique<ast::pattern::Pattern>(*std::move(value)),
      });

      TRY(parser.expected<token::Type::comma, token::Type::right_brace>());
      auto _ = parser.expect<token::Type::comma>();
    }

    return ast::pattern::Pack{.tagged_values = std::move(tagged_values)};
  }());
}

std::expected<ast::pattern::Binding, ParseError> parse_binding(Parser &parser) noexcept {
  TRY_DEF(name, parser.expect<token::Type::id>());
  return ast::pattern::Binding{.name = name->view};
}

std::expected<ast::pattern::Pattern, ParseError> parse_pattern(Parser &parser) noexcept {
  return parse_any<ast::pattern::Pattern>(parser, parse_tagged_value, parse_pack, parse_binding);
}

} // namespace pattern

// This is left-associative.
// app := primary_expr (primary_expr)+
// case_step := pattern "->" expr ("," case_step | "}") | "}"
// case := "case" ! expr "of" "{" case_step
// tagged_value := ":" ! (id expr | id)
// pack_step := id "=" pattern ("," pack_step | "}") | "}"
// pack := "{" ! pack_step
// binding := id "::" type | id
// lambda := "|" ! binding "|" expr
// tv_lambda := "[" ! (id "::" kind | id) "]" expr
// ref := id
// primary_expr := case | tagged_value | pack | lambda | tv_lambda | ref | "(" expr ")"
// expr := app | primary_expr
namespace expr {

std::expected<ast::expr::Expr, ParseError> parse_primary_expr(Parser &parser) noexcept;
std::expected<ast::expr::Expr, ParseError> parse_expr(Parser &parser) noexcept;

std::expected<ast::expr::Application, ParseError> parse_application(Parser &parser) noexcept {
  TRY_DEF(function, parse_primary_expr(parser));
  TRY_DEF(argument, parse_primary_expr(parser));
  ast::expr::Application app{
      .function = std::make_unique<ast::expr::Expr>(*std::move(function)),
      .argument = std::make_unique<ast::expr::Expr>(*std::move(argument)),
  };
  while (auto next_argument = parse_primary_expr(parser)) {
    app = ast::expr::Application{
        .function = std::make_unique<ast::expr::Expr>(std::move(app)),
        .argument = std::make_unique<ast::expr::Expr>(*std::move(next_argument)),
    };
  }
  return app;
}

std::expected<ast::expr::Case, ParseError> parse_case(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::case_>());

  return cut([&] -> decltype(parse_case(parser)) {
    TRY_DEF(scrutinee, parse_expr(parser));
    TRY(parser.expect<token::Type::of>());
    TRY(parser.expect<token::Type::left_brace>());

    move_only_vector<ast::expr::Choice> choices;

    while (true) {
      if (parser.expect<token::Type::right_brace>()) {
        break;
      }

      TRY_DEF(pattern, pattern::parse_pattern(parser));
      TRY(parser.expect<token::Type::arrow>());
      TRY_DEF(result, parse_expr(parser));

      choices.push_back({
          .pattern = *std::move(pattern),
          .result = std::make_unique<ast::expr::Expr>(*std::move(result)),
      });

      auto _ = parser.expect<token::Type::comma>();
    }

    return ast::expr::Case{
        .scrutinee = std::make_unique<ast::expr::Expr>(*std::move(scrutinee)),
        .choices = std::move(choices),
    };
  }());
}

std::expected<ast::expr::TaggedValue, ParseError> parse_tagged_value(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::colon>());
  TRY_DEF(tag, cut(parser.expect<token::Type::id>().transform_error(perr)));
  if (auto value = parse_expr(parser)) {
    return ast::expr::TaggedValue{
        .tag = tag->view,
        .value = std::make_unique<ast::expr::Expr>(*std::move(value)),
    };
  } else {
    return ast::expr::TaggedValue{
        .tag = tag->view,
        .value = std::make_unique<ast::expr::Expr>(ast::expr::Pack{}),
    };
  }
}

std::expected<ast::expr::Pack, ParseError> parse_pack(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::left_brace>());

  return cut([&] -> decltype(parse_pack(parser)) {
    move_only_vector<ast::expr::TaggedValue> tagged_values;

    while (true) {
      TRY(parser.expected<token::Type::id, token::Type::right_brace>());
      if (parser.expect<token::Type::right_brace>()) {
        break;
      }

      TRY_DEF(tag, parser.expect<token::Type::id>());
      TRY(parser.expect<token::Type::equal>());
      TRY_DEF(value, parse_expr(parser));

      tagged_values.push_back({
          .tag = tag->view,
          .value = std::make_unique<ast::expr::Expr>(*std::move(value)),
      });

      TRY(parser.expected<token::Type::comma, token::Type::right_brace>());
      auto _ = parser.expect<token::Type::comma>();
    }

    return ast::expr::Pack{.tagged_values = std::move(tagged_values)};
  }());
}

std::expected<ast::expr::Binding, ParseError> parse_binding(Parser &parser) noexcept {
  TRY_DEF(name, parser.expect<token::Type::id>());
  if (parser.expect<token::Type::has_type>()) {
    TRY_DEF(type, type::parse_type(parser));
    return ast::expr::Binding{
        .name = name->view,
        .type = *std::move(type),
    };
  } else {
    return ast::expr::Binding{
        .name = name->view,
        .type = std::nullopt,
    };
  }
}

std::expected<ast::expr::Lambda, ParseError> parse_lambda(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::pipe>());
  return cut([&] -> decltype(parse_lambda(parser)) {
    TRY_DEF(binding, parse_binding(parser));
    TRY(parser.expect<token::Type::pipe>());
    TRY_DEF(body, parse_expr(parser));
    return ast::expr::Lambda{
        .binding = *std::move(binding),
        .body = std::make_unique<ast::expr::Expr>(*std::move(body)),
    };
  }());
}

std::expected<ast::expr::TVLambda, ParseError> parse_tv_lambda(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::left_bracket>());
  return cut([&] -> decltype(parse_tv_lambda(parser)) {
    TRY_DEF(type_binding, parser.expect<token::Type::id>());
    ast::kind::Kind kind = ast::kind::Type{};
    if (parser.expect<token::Type::has_type>()) {
      TRY_DEF(parsed_kind, kind::parse_kind(parser));
      kind = *std::move(parsed_kind);
    }
    TRY(parser.expect<token::Type::right_bracket>());
    TRY_DEF(body, parse_expr(parser));
    return ast::expr::TVLambda{
        .type_binding =
            ast::type::Binding{
                .name = type_binding->view,
                .kind = std::move(kind),
            },
        .body = std::make_unique<ast::expr::Expr>(*std::move(body)),
    };
  }());
}

std::expected<ast::expr::ValueOrBindingReference, ParseError>
parse_value_or_binding_reference(Parser &parser) noexcept {
  TRY_DEF(name, parser.expect<token::Type::id>());
  return ast::expr::ValueOrBindingReference{.name = name->view};
}

std::expected<ast::expr::Expr, ParseError> parse_primary_expr(Parser &parser) noexcept {
  return parse_any<ast::expr::Expr>(parser, parse_case, parse_tagged_value, parse_pack,
                                    parse_lambda, parse_tv_lambda, parse_value_or_binding_reference,
                                    parenthesized(parse_expr));
}

std::expected<ast::expr::Expr, ParseError> parse_expr(Parser &parser) noexcept {
  return parse_any<ast::expr::Expr>(parser, parse_application, parse_primary_expr);
}

} // namespace expr

// type_binding := id | "(" id "::" kind ")"
// type_definition := "form" ! id type_binding* "=" type
// declaration := "dec" ! id "::" type
// tv_type_binding := "[" (id "::" kind | id) "]"
// binding := id | "(" id :: type ")"
// definition := "def" ! id tv_type_binding* binding* "=" expr
// value_definition := declaration definition | definition
// entity := type_definition | value_definition
namespace entity {

std::expected<ast::entity::TypeDefinition, ParseError>
parse_type_definition(Parser &parser) noexcept {
  TRY(parser.expect<token::Type::form>());

  return cut([&] -> decltype(parse_type_definition(parser)) {
    TRY_DEF(definition_name, parser.expect<token::Type::id>());

    move_only_vector<ast::type::Binding> type_bindings;
    while (true) {
      TRY(parser.expected<token::Type::id, token::Type::left_paren, token::Type::equal>());
      if (parser.expect<token::Type::equal>()) {
        break;
      }

      if (parser.expect<token::Type::left_paren>()) {
        TRY_DEF(name, parser.expect<token::Type::id>());
        TRY(parser.expect<token::Type::has_type>());
        TRY_DEF(kind, kind::parse_kind(parser));
        type_bindings.push_back({
            .name = name->view,
            .kind = *std::move(kind),
        });
        TRY(parser.expect<token::Type::right_paren>());
      } else {
        TRY_DEF(name, parser.expect<token::Type::id>());
        type_bindings.push_back({
            .name = name->view,
            .kind = ast::kind::Type{},
        });
      }
    }

    TRY_DEF(type, type::parse_type(parser));

    return ast::entity::TypeDefinition{
        .name = definition_name->view,
        .type_bindings = std::move(type_bindings),
        .type = *std::move(type),
    };
  }());
}

std::expected<ast::entity::ValueDefinition, ParseError>
parse_value_definition(Parser &parser) noexcept {
  // TODO: Stuff like this can be automated.
  TRY(parser.expected<token::Type::dec, token::Type::def>());

  std::optional<std::pair<std::string_view, ast::type::Type>> dec;
  if (parser.expect<token::Type::dec>()) {
    TRY(cut([&] -> std::expected<void, ParseError> {
      TRY_DEF(name, parser.expect<token::Type::id>());
      TRY(parser.expect<token::Type::has_type>());
      TRY_DEF(type, type::parse_type(parser));
      dec = std::make_pair(name->view, *std::move(type));
      TRY(parser.expect<token::Type::def>());
      return {};
    }()));
  } else {
    TRY(parser.expect<token::Type::def>());
  }

  return cut([&] -> decltype(parse_value_definition(parser)) {
    TRY_DEF(name, parser.expect<token::Type::id>());
    if (dec and dec->first != name->view) {
      return std::unexpected(UnexpectedToken{
          // TODO: Perhaps expected should be a list of strings
          .expected = {token::Type::id},
          .got = *name,
      });
    }

    // TODO: Stuff like this can be automated.
    TRY(parser.expected<token::Type::left_bracket, token::Type::id, token::Type::left_paren,
                        token::Type::equal>());

    move_only_vector<ast::type::Binding> type_bindings;
    while (parser.expect<token::Type::left_bracket>()) {
      TRY_DEF(type_binding, parser.expect<token::Type::id>());
      ast::kind::Kind kind = ast::kind::Type{};
      if (parser.expect<token::Type::has_type>()) {
        TRY_DEF(parsed_kind, kind::parse_kind(parser));
        kind = *std::move(parsed_kind);
      }
      type_bindings.push_back({
          .name = type_binding->view,
          .kind = std::move(kind),
      });
      TRY(parser.expect<token::Type::right_bracket>());
    }

    // TODO: Stuff like this can be automated.
    TRY(parser.expected<token::Type::id, token::Type::left_paren, token::Type::equal>());

    move_only_vector<ast::expr::Binding> bindings;
    while (parser.expected<token::Type::id, token::Type::left_paren>()) {
      if (parser.expect<token::Type::left_paren>()) {
        TRY_DEF(binding_name, parser.expect<token::Type::id>());
        TRY(parser.expect<token::Type::has_type>());
        TRY_DEF(type, type::parse_type(parser));
        bindings.push_back(ast::expr::Binding{
            .name = binding_name->view,
            .type = *std::move(type),
        });
        TRY(parser.expect<token::Type::right_paren>());
      } else if (auto binding_name = parser.expect<token::Type::id>()) {
        bindings.push_back(ast::expr::Binding{
            .name = binding_name->view,
            .type = std::nullopt,
        });
      }
    }

    TRY(parser.expect<token::Type::equal>());

    TRY_DEF(value, expr::parse_expr(parser));

    return ast::entity::ValueDefinition{
        .type = dec.transform([](auto &p) { return std::move(p.second); }),
        .name = name->view,
        .type_bindings = std::move(type_bindings),
        .bindings = std::move(bindings),
        .value = *std::move(value),
    };
  }());
}

std::expected<ast::entity::Entity, ParseError> parse_entity(Parser &parser) noexcept {
  return parse_any<ast::entity::Entity>(parser, parse_type_definition, parse_value_definition);
}

} // namespace entity

} // namespace

export std::expected<move_only_vector<ast::entity::Entity>, ParseError>
parse(std::string_view view) noexcept {
  token::Tokenizer tokenizer{view};
  Parser parser{tokenizer};

  move_only_vector<ast::entity::Entity> entities;
  while (true) {
    if (parser.expect<token::Type::end>()) {
      break;
    }

    TRY_DEF(entity, entity::parse_entity(parser));
    entities.push_back(*std::move(entity));
  }

  return entities;
}

} // namespace raw_parser
