#include <catch2/catch_test_macros.hpp>

import id;
import type;
import type_storage;

TEST_CASE("Distinct types equal after merging unequal parts.", "[type_storage]") {
  type_storage::TypeStorage ts;
  auto const id = id::TypeId::unit_id;
  auto const a_id = ts.store(type::Arrow{id, id});
  auto const var = ts.make_variable();
  auto const b_id = ts.store(type::Arrow{var, id});
  REQUIRE_FALSE(ts.equal(a_id, b_id));
  ts.merge_into(var, id);
  CHECK(ts.equal(a_id, b_id));
}
