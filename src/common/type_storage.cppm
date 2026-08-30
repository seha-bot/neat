module;

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

export module type_storage;

import id;
import move_only_vector;
import todo;
import type;

export namespace type_storage {

struct RepresentativeSets {
private:
  struct Hasher {
    static std::size_t operator()(id::TypeId t) noexcept { return t.value; }
  };
  struct Eq {
    static bool operator()(id::TypeId a, id::TypeId b) noexcept { return a.value == b.value; }
  };
  using Map = std::unordered_map<id::TypeId, id::TypeId, Hasher, Eq>;

public:
  id::TypeId representative(id::TypeId a) { return representative_iterator(a)->first; }

  /// Merges `a` into `b`.
  void merge_into(id::TypeId a, id::TypeId b) {
    representative_iterator(a)->second = representative(b);
  }

private:
  Map::iterator representative_iterator(id::TypeId a) {
    std::unordered_set<id::TypeId, Hasher, Eq> seen;
    while (true) {
      if (not seen.insert(a).second) {
        todo();
      }

      auto [it, _] = m_root.insert({a, a});
      auto &a_parent = it->second;
      if (a_parent.value == a.value) {
        return it;
      } else {
        // TODO: path compression.
        // return a_parent = representative(a_parent);
        a = a_parent;
      }
    }
  }

  Map m_root;
};

/// Its only purpose is to store your types.
/// NOTE: Uniqueness of stored types is not guaranteed.
/// assert(ts.equal(a, b));
/// assert(&ts.read(a) == &ts.read(b)); // Might not pass.
struct TypeStorage {
  TypeStorage() = default;
  TypeStorage(TypeStorage const &) = delete;
  TypeStorage(TypeStorage &&) = default;
  TypeStorage &operator=(TypeStorage const &) = delete;
  TypeStorage &operator=(TypeStorage &&) = default;
  ~TypeStorage() = default;

  // TODO: In order to reduce the amount of types stored, you could keep a vector
  // which records which ids are variables so you may recycle them while typechecking the next
  // definition.
  [[nodiscard]] id::VariableId make_variable() {
    id::TypeId id{m_types.size()};
    m_types.push_back(type::Variable{});
    return id::VariableId{id};
  }

  [[nodiscard]] id::TypeId make_rigid_variable() {
    id::TypeId id{m_types.size()};
    m_types.push_back(type::RigidVariable{});
    return id;
  }

  [[nodiscard]] id::TypeId store_new(type::Type type) {
    id::TypeId id{m_types.size()};
    m_types.push_back(std::move(type));
    return id;
  }

  [[nodiscard]] id::TypeId store(type::Type type) {
    for (std::size_t i = 0; i < m_types.size(); ++i) {
      if (type_equal(m_types[i], type)) {
        return id::TypeId{i};
      }
    }
    return store_new(std::move(type));
  }

  [[nodiscard]] type::Type const &read(id::TypeId id) const {
    return read_exact(m_rep.representative(id));
  }

  [[nodiscard]] std::optional<id::VariableId> is_variable(id::TypeId id) const {
    if (std::holds_alternative<type::Variable>(read(id))) {
      return id::VariableId{id};
    } else {
      return std::nullopt;
    }
  }

  [[nodiscard]] bool equal(id::TypeId a_id, id::TypeId b_id) const {
    auto const a_rep_id = m_rep.representative(a_id);
    auto const b_rep_id = m_rep.representative(b_id);
    if (a_rep_id.value == b_rep_id.value) {
      return true;
    }
    return type_equal(read_exact(a_rep_id), read_exact(b_rep_id));
  }

  // If b_id represents a variable, then merge_into is commutative.
  void merge_into(id::VariableId a_id, id::TypeId b_id) { m_rep.merge_into(a_id, b_id); }

  /// Replaces all DeBruijn indices pointing to the current root of type_id with subst_id.
  [[nodiscard]] id::TypeId instantiate(id::TypeId type_id, id::TypeId subst_id) {
    return instantiate_impl(type_id, subst_id, 0);
  }

  [[nodiscard]] id::KindId store_kind(kind::Kind kind) noexcept {
    id::KindId id{m_kinds.size()};
    m_kinds.push_back(std::move(kind));
    return id;
  }

  [[nodiscard]] kind::Kind const &read_kind(id::KindId kind_id) const noexcept {
    return m_kinds.at(kind_id.value);
  }

private:
  id::TypeId instantiate_impl(id::TypeId type_id, id::TypeId subst_id, std::size_t depth) {
    struct Visitor {
      id::TypeId operator()(type::Arrow arr) {
        return ts.store(type::Arrow{
            ts.instantiate_impl(arr.from_id, subst_id, depth),
            ts.instantiate_impl(arr.to_id, subst_id, depth),
        });
      }
      id::TypeId operator()(type::ForAll forall) {
        return ts.store(type::ForAll{
            .binding_kind_id = forall.binding_kind_id,
            .type_id = ts.instantiate_impl(forall.type_id, subst_id, depth + 1),
        });
      }
      id::TypeId operator()(type::DeBruijnIndex index) {
        return index.value == depth ? subst_id : type_id;
      }
      id::TypeId operator()(type::Union const &v) {
        move_only_vector<type::Element> elements;
        elements.reserve(v.elements.size());
        for (auto &e : v.elements) {
          elements.push_back({
              .tag_id = e.tag_id,
              .type_id = ts.instantiate_impl(e.type_id, subst_id, depth),
          });
        }
        return ts.store(type::Union{std::move(elements)});
      }
      id::TypeId operator()(type::Struct const &s) {
        move_only_vector<type::Element> elements;
        elements.reserve(s.elements.size());
        for (auto &e : s.elements) {
          elements.push_back({
              .tag_id = e.tag_id,
              .type_id = ts.instantiate_impl(e.type_id, subst_id, depth),
          });
        }
        return ts.store(type::Struct{std::move(elements)});
      }
      id::TypeId operator()(type::TTLambda tt_lambda) {
        return ts.store(type::TTLambda{
            .binding_kind_id = tt_lambda.binding_kind_id,
            .type_id = ts.instantiate_impl(tt_lambda.type_id, subst_id, depth + 1),
        });
      }
      id::TypeId operator()(type::Application app) {
        return ts.store(type::Application{
            .function_id = ts.instantiate_impl(app.function_id, subst_id, depth),
            .argument_id = ts.instantiate_impl(app.argument_id, subst_id, depth),
        });
      }
      id::TypeId operator()(type::Variable const &) { return type_id; }
      id::TypeId operator()(type::RigidVariable const &) { return type_id; }
      id::TypeId operator()(type::NamedTypeReference const &) { return type_id; }

      TypeStorage &ts;
      id::TypeId type_id;
      id::TypeId subst_id;
      std::size_t depth;
    };
    return std::visit(Visitor{*this, type_id, subst_id, depth}, read(type_id));
  }

  struct EqualVisitor {
    bool operator()(type::Arrow const &a, type::Arrow const &b) {
      return ts.equal(a.from_id, b.from_id) and ts.equal(a.to_id, b.to_id);
    }
    bool operator()(type::ForAll const &a, type::ForAll const &b) {
      return ts.equal(a.type_id, b.type_id);
    }
    bool operator()(type::DeBruijnIndex const &a, type::DeBruijnIndex const &b) {
      return a.value == b.value;
    }
    bool operator()(type::Union const &a, type::Union const &b) {
      return std::ranges::all_of(a.elements, [&](type::Element const &e1) {
        return std::ranges::any_of(b.elements, [&](type::Element const &e2) {
          return e1.tag_id == e2.tag_id and ts.equal(e1.type_id, e2.type_id);
        });
      });
    }
    bool operator()(type::Struct const &a, type::Struct const &b) {
      return std::ranges::equal(
          a.elements, b.elements, [&](type::Element const &e1, type::Element const &e2) {
            return e1.tag_id == e2.tag_id and ts.equal(e1.type_id, e2.type_id);
          });
    }
    bool operator()(type::TTLambda const &a, type::TTLambda const &b) {
      return ts.equal(a.type_id, b.type_id);
    }
    bool operator()(type::Application const &a, type::Application const &b) {
      return ts.equal(a.function_id, b.function_id) and ts.equal(a.argument_id, b.argument_id);
    }
    bool operator()(type::NamedTypeReference const &a, type::NamedTypeReference const &b) {
      return a.form_id == b.form_id;
    }

    // Exhaustive alternatives for non-equal types.
    bool operator()(type::Arrow const &, auto &) { return false; }
    bool operator()(type::ForAll const &, auto &) { return false; }
    bool operator()(type::DeBruijnIndex const &, auto &) { return false; }
    bool operator()(type::Union const &, auto &) { return false; }
    bool operator()(type::Struct const &, auto &) { return false; }
    bool operator()(type::TTLambda const &, auto &) { return false; }
    bool operator()(type::Application const &, auto &) { return false; }
    bool operator()(type::Variable const &, auto &) { return false; }
    bool operator()(type::RigidVariable const &, auto &) { return false; }
    bool operator()(type::NamedTypeReference const &, auto &) { return false; }

    TypeStorage const &ts;
  };

  bool type_equal(type::Type const &a, type::Type const &b) const {
    return std::visit(EqualVisitor{*this}, a, b);
  }

  type::Type const &read_exact(id::TypeId id) const {
    if (id.value == id::TypeId::unit_id.value) {
      static type::Type const unit{type::Struct{}};
      return unit;
    }
    return m_types.at(id.value);
  }

  // FIX: MAKE PRIVATE!
public:
  mutable RepresentativeSets m_rep;
  move_only_vector<type::Type> m_types;
  move_only_vector<kind::Kind> m_kinds;
};

} // namespace type_storage
