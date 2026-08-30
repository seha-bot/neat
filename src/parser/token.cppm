module;

#include <cstddef>
#include <optional>
#include <string_view>

export module token;

import source;

namespace token {

export enum class Type : unsigned char {
  /// The last token. Ever.
  end,
  /// An identifier.
  id,
  comment,
  form,
  dec,
  def,
  equal,
  arrow,
  star,
  pipe,
  has_type,
  comma,
  left_splice,
  right_splice,
  colon,
  left_paren,
  right_paren,
  left_bracket,
  right_bracket,
  left_brace,
  right_brace,
  case_,
  of,
};

struct TypeInfo {
  Type type;
  std::string_view name;
};

/// It is important that this is in the same order as the enumerators in Type.
constexpr TypeInfo info[] = {
    {Type::end, "end"},         {Type::id, "id"},           {Type::comment, "#"},
    {Type::form, "form"},       {Type::dec, "dec"},         {Type::def, "def"},
    {Type::equal, "="},         {Type::arrow, "->"},        {Type::star, "*"},
    {Type::pipe, "|"},          {Type::has_type, "::"},     {Type::comma, ","},
    {Type::left_splice, "[:"},  {Type::right_splice, ":]"}, {Type::colon, ":"},
    {Type::left_paren, "("},    {Type::right_paren, ")"},   {Type::left_bracket, "["},
    {Type::right_bracket, "]"}, {Type::left_brace, "{"},    {Type::right_brace, "}"},
    {Type::case_, "case"},      {Type::of, "of"},
};

/// These tokens are capable of separating other tokens.
/// For example: a|a is tokenized as id, pipe, id.
/// It is important that longer ones come before shorter ones if they're contained in one another.
constexpr Type separators[] = {
    Type::equal,       Type::arrow,        Type::star,          Type::pipe,       Type::has_type,
    Type::comma,       Type::left_splice,  Type::right_splice,  Type::colon,      Type::left_paren,
    Type::right_paren, Type::left_bracket, Type::right_bracket, Type::left_brace, Type::right_brace,
};

[[nodiscard]] constexpr Type parse_type(std::string_view view) noexcept {
  for (auto x : info) {
    if (x.type != Type::end and x.type != Type::id and x.type != Type::comment and x.name == view) {
      return x.type;
    }
  }
  return Type::id;
}

constexpr bool is_space(char c) noexcept { return c == ' ' or c == '\n' or c == '\t' or c == '\r'; }

export constexpr std::string_view type_to_string(Type type) noexcept {
  return info[static_cast<std::size_t>(type)].name;
}

export struct Token {
  std::string_view view;
  source::Range range;
  Type type;
};

export struct Checkpoint {
  source::Position pos;
  std::size_t index;
};

export struct Tokenizer {
  constexpr Tokenizer(std::string_view view) noexcept : m_view(view), m_pos{1, 1}, m_index(0) {}

  [[nodiscard]] constexpr Checkpoint checkpoint() const noexcept { return {m_pos, m_index}; }

  constexpr void restore(Checkpoint cp) noexcept {
    m_pos = cp.pos;
    m_index = cp.index;
  }

  [[nodiscard]] constexpr token::Token next() noexcept {
    while (m_index < m_view.size()) {
      if (is_space(m_view[m_index])) {
        eat();
      } else if (m_view[m_index] == '#') {
        while (m_index < m_view.size() and m_view[m_index] != '\n') {
          eat();
        }
      } else {
        break;
      }
    }

    auto const start_pos = m_pos;
    auto const start_index = m_index;

    auto end_pos = m_pos;
    auto end_index = m_index;

    auto leading_separator = [this] -> std::optional<Type> {
      for (auto sep : token::separators) {
        if (m_view.substr(m_index).starts_with(type_to_string(sep))) {
          return sep;
        }
      }
      return std::nullopt;
    };

    while (m_index < m_view.size() and not is_space(m_view[m_index]) and m_view[m_index] != '#') {
      if (auto sep = leading_separator()) {
        if (start_index != m_index) {
          break;
        }
        auto sep_str = type_to_string(*sep);
        m_index += sep_str.size();
        m_pos.col += sep_str.size();
        return token::Token{
            .view = m_view.substr(start_index, sep_str.size()),
            .range = {start_pos, {m_pos.line, m_pos.col - 1}},
            .type = *sep,
        };
      }

      end_pos = m_pos;
      end_index = m_index;
      eat();
    }

    if (start_index == m_index) {
      return token::Token{
          .view = "",
          .range = {m_pos, m_pos},
          .type = Type::end,
      };
    }

    auto const subview = m_view.substr(start_index, end_index + 1 - start_index);
    auto const type = token::parse_type(subview);

    return token::Token{
        .view = subview,
        .range = {start_pos, end_pos},
        .type = type,
    };
  }

private:
  constexpr void eat() noexcept {
    if (m_view[m_index] == '\n') {
      ++m_pos.line;
      m_pos.col = 0;
    }
    ++m_pos.col;
    ++m_index;
  }

  std::string_view m_view;
  source::Position m_pos;
  std::size_t m_index;
};

} // namespace token
