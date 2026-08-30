#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

import formatter;
import kindchecker;
import parser;
import token;
import typechecker;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: neat <source>\n";
    return EXIT_FAILURE;
  }
  std::string const filename = argv[1];

  std::string source;
  {
    std::ifstream const f(filename);
    if (not f) {
      std::cerr << "Can't open file \"" << filename << "\" for reading.";
      return EXIT_FAILURE;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    source = std::move(buffer).str();
  }

  // token::Tokenizer tokenizer{source};
  // while (true) {
  //   auto tok = tokenizer.next();
  //   if (tok.type == token::Type::end) {
  //     break;
  //   }
  //   std::cout << tok.view << ' ' << type_to_string(tok.type) << '\n';
  // }
  // return 0;

  auto resolved_ast = parser::parse(source);
  if (not resolved_ast) {
    std::cerr << resolved_ast.error();
    return EXIT_FAILURE;
  }
  formatter::Context const ctx{resolved_ast->ts, resolved_ast->forms, resolved_ast->tags};
  for (auto &form : resolved_ast->forms) {
    std::cout << formatter::format_form(ctx, form) << '\n';
  }
  for (auto &value : resolved_ast->values) {
    std::cout << formatter::format_value({ctx, resolved_ast->values}, value) << '\n';
  }

  kindchecker::check(resolved_ast->ts, resolved_ast->forms);

  auto type_env = typechecker::typecheck(resolved_ast->ts, resolved_ast->tags, resolved_ast->forms,
                                         std::move(resolved_ast->values));
  // if (not type_env) {
  //   std::cerr << type_env.error() << '\n';
  //   return EXIT_FAILURE;
  // }
}
