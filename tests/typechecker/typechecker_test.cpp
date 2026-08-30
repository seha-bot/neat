#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <utility>

import parser;
import typechecker;

auto parse(std::string_view expr) {
  auto ast = parser::parse(expr);
  REQUIRE(ast.has_value());
  return *std::move(ast);
}

TEST_CASE("Casting.", "[typechecker]") {
  auto ast = parse(R"(
    def f[T] (x :: T) = x
    def g[T] = (|x :: T -> T| x) f
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE(result.has_value());
}

TEST_CASE("More casting.", "[typechecker]") {
  auto ast = parse(R"(
    form Either A B = :left A | :right B
    def id[A] (x :: A) = x

    dec left :: [A][B] A -> Either A B
    def left[_][_] x = :left x

    def left'[A][B] (x :: A) = (|y :: Either A B| y) (:left x)

    def left''[A][B] (x :: A) = id[:Either A B:] (:left x)
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE(result.has_value());
}

TEST_CASE("Forbid uninferred types.", "[typechecker]") {
  auto ast = parse(R"(
    def id x = x
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Forbid type constructors where types are expected.", "[typechecker]") {
  auto ast = parse(R"(
    form Id A = A
    dec f Id
    def f = f
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Forbid escape of type parameters.", "[typechecker]") {
  auto ast = parse(R"(
    def f x = [a] (|y :: a| y) x
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Fib.", "[typechecker]") {
  auto ast = parse(R"(
    form Nat = :zero | :succ Nat

    def add (a :: Nat) (b :: Nat) = case b of {
      :zero -> a,
      :succ b' -> :succ (add a b'),
    }

    def fib (n :: Nat) = case n of {
      :zero -> n,
      :succ :zero -> n,
      :succ (:succ m) -> add (fib (:succ m)) (fib m),
    }
  )");
  auto result = typechecker::typecheck(ast.ts, ast.tags, ast.forms, std::move(ast.values));
  // FIX:
  // REQUIRE(result.has_value());
}
