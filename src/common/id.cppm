module;

#include <cstddef>
#include <functional>

export module id;

export namespace id {

enum class Domain {
  form,
  kind,
  tag,
  type,
  value,
};

template <Domain D> struct Id {
  static constexpr Domain domain = D;
  std::size_t value;
};

struct FormId : Id<Domain::form> {
  bool operator==(FormId const &id) const { return value == id.value; }
};

struct KindId : Id<Domain::kind> {
  bool operator==(KindId const &id) const { return value == id.value; }
};

struct ValueId : Id<Domain::value> {
  bool operator==(ValueId const &id) const { return value == id.value; }
};

struct TagId : Id<Domain::tag> {
  bool operator==(TagId const &id) const { return value == id.value; }
  bool operator!=(TagId const &id) const { return value != id.value; }
  bool operator<(TagId const &id) const { return value < id.value; }
  bool operator>(TagId const &id) const { return value > id.value; }
  bool operator<=(TagId const &id) const { return value <= id.value; }
  bool operator>=(TagId const &id) const { return value >= id.value; }
};

struct TypeId : Id<Domain::type> {
  /// Special value which represents the unit type.
  static const TypeId unit_id;
};

constexpr TypeId TypeId::unit_id{{
    .value = static_cast<std::size_t>(-1),
}};

struct VariableId : TypeId {};

} // namespace id

template <id::Domain D> struct std::hash<id::Id<D>> {
  static std::size_t operator()(id::Id<D> const &id) noexcept { return id.value; }
};
