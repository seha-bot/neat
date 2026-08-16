module;

#include <algorithm>
#include <deque>
#include <functional>
#include <ostream>
#include <variant>
#include <vector>

export module constraint;

import entity;
import id;
import move_only_vector;
import todo;
import type;
import type_storage;

namespace constraint {

export struct SubtypeOf {
  // Represents the relation a <= b.
  // 1. For every type a, a <= a is true.
  // 2. If a <= b and b <= c, then a <= c is true.
  // 3. If a <= b and b <= a, then a and b are the same type.
  id::TypeId a_id, b_id;

  bool equal(type_storage::TypeStorage const &ts, SubtypeOf const &that) {
    return ts.equal(a_id, that.a_id) and ts.equal(b_id, that.b_id);
  }

  SubtypeOf flip() const { return {b_id, a_id}; }

  void log(std::ostream &os, auto log) const {
    log(os, a_id);
    os << " <= ";
    log(os, b_id);
  }
};

namespace {

struct SubtypeOfRule {
  template <typename A, typename B> void operator()(A const &a, B const &b) {
    if constexpr (std::same_as<A, type::Arrow> and std::same_as<B, type::Arrow>) {
      constraints.push_back(SubtypeOf{b.from_id, a.from_id});
      constraints.push_back(SubtypeOf{a.to_id, b.to_id});
    } else if constexpr (std::same_as<A, type::ForAll> and std::same_as<B, type::ForAll>) {
      constraints.push_back(SubtypeOf{a.type_id, b.type_id});
    } else if constexpr (std::same_as<A, type::DeBruijnIndex> and
                         std::same_as<B, type::DeBruijnIndex>) {
      if (a.value != b.value) {
        todo();
      }
    } else if constexpr (std::same_as<A, type::ForAll> and std::same_as<B, type::Arrow>) {
      constraints.push_back(SubtypeOf{ts.instantiate(a.type_id, ts.make_variable()), b_id});
    } else if constexpr (std::same_as<A, type::Union> and std::same_as<B, type::Union>) {
      for (auto &e1 : a.elements) {
        auto it = std::ranges::find(b.elements, e1.tag_id, &type::Element::tag_id);
        if (it == b.elements.end()) {
          todo();
        }
        constraints.push_back(SubtypeOf{e1.type_id, it->type_id});
      }
    } else if constexpr (std::same_as<A, type::Struct> and std::same_as<B, type::Struct>) {
      for (auto &e2 : b.elements) {
        auto it = std::ranges::find(a.elements, e2.tag_id, &type::Element::tag_id);
        if (it == a.elements.end()) {
          todo();
        }
        constraints.push_back(SubtypeOf{it->type_id, e2.type_id});
      }
    } else if constexpr (std::same_as<A, type::NamedTypeReference> and
                         std::same_as<B, type::NamedTypeReference>) {
      if (a.form_id != b.form_id) {
        todo();
      }
    } else if constexpr (std::same_as<A, type::Application> and
                         std::same_as<B, type::Application>) {
      // FIX: Faulty. What if one of these isn't deduced yet?
      if (not ts.equal(a.function_id, b.function_id)) {
        todo();
      }
      constraints.push_back(SubtypeOf{a.argument_id, b.argument_id});
    } else if constexpr (std::same_as<B, type::NamedTypeReference>) {
      constraints.push_back(SubtypeOf{a_id, forms[b.form_id.value].type_id});
    } else if constexpr (std::same_as<B, type::Application>) {
      constraints.push_back(SubtypeOf{a_id, ts.instantiate(b.function_id, b.argument_id)});
    } else {
      // static_assert(false);
      todo();
    }
  }

  move_only_vector<entity::TypeDefinition> const &forms;
  type_storage::TypeStorage &ts;
  std::deque<SubtypeOf> &constraints;
  id::TypeId a_id;
  id::TypeId b_id;
};

} // namespace

export struct Solver {
  void add_constraint(SubtypeOf c) { m_constraints.push_back(c); }

  void solve(std::ostream &os, std::function<void(std::ostream &os, id::TypeId)> log,
             move_only_vector<entity::TypeDefinition> const &forms, type_storage::TypeStorage &ts) {
    while (not m_constraints.empty()) {
      for (std::size_t i = 0; i < m_constraints.size(); ++i) {
        for (std::size_t j = i + 1; j < m_constraints.size(); ++j) {
          if (m_constraints[i].equal(ts, m_constraints[j])) {
            os << "Erasing (duplicate): ";
            m_constraints[i].log(os, log);
            os << '\n';
            m_constraints.erase(m_constraints.begin() + static_cast<std::ptrdiff_t>(j));
            --j;
          }
        }
      }

      {
        auto constraints = std::move(m_constraints);
        m_constraints.clear();
        bool did_something = false;

        while (not constraints.empty()) {
          auto c = constraints.front();
          constraints.pop_front();

          c.log(os, log);
          os << '\n';

          auto &a = ts.read(c.a_id);
          auto &b = ts.read(c.b_id);
          if (std::holds_alternative<type::Variable>(a) or
              std::holds_alternative<type::Variable>(b)) {
            m_constraints.push_back(c);
          } else {
            std::visit(SubtypeOfRule{forms, ts, m_constraints, c.a_id, c.b_id}, a, b);
            did_something = true;
          }
        }

        if (did_something) {
          os << "Again...\n";
          continue;
        }
      }

      // At this point, every constraint contains at least 1 variable.
      for (std::size_t i = 0; i < m_constraints.size(); ++i) {
        auto [a_id, b_id] = m_constraints[i];
        if (ts.equal(a_id, b_id)) {
          m_constraints.erase(m_constraints.begin() + static_cast<std::ptrdiff_t>(i));
          --i;
        }
      }

      struct Bounds {
        std::vector<id::TypeId> lower, upper;
        id::TypeId middle;
      };
      std::unordered_map<type::Variable const *, Bounds> var_bounds;

      for (auto [a_id, b_id] : m_constraints) {
        if (auto *a = std::get_if<type::Variable>(&ts.read(a_id))) {
          var_bounds[a].upper.push_back(b_id);
          var_bounds[a].middle = a_id;
        }
        if (auto *b = std::get_if<type::Variable>(&ts.read(b_id))) {
          var_bounds[b].lower.push_back(a_id);
          var_bounds[b].middle = b_id;
        }
      }

      os << "Calculated bounds:\n";
      for (auto &[_, bounds] : var_bounds) {
        for (std::size_t i = 0; i < bounds.lower.size(); ++i) {
          if (i != 0) {
            os << ", ";
          }
          log(os, bounds.lower[i]);
        }
        os << " <= ";
        log(os, bounds.middle);
        os << " <= ";
        for (std::size_t i = 0; i < bounds.upper.size(); ++i) {
          if (i != 0) {
            os << ", ";
          }
          log(os, bounds.upper[i]);
        }
        os << '\n';
      }

      struct MergeRequest {
        id::TypeId a_id, b_id;
      };
      std::vector<MergeRequest> merge_requests;
      for (auto &[_, bounds] : var_bounds) {
        bool did_constrain = false;
        for (std::size_t i = 0; i < bounds.lower.size(); ++i) {
          for (std::size_t j = 0; j < bounds.upper.size(); ++j) {
            if (ts.equal(bounds.lower[i], bounds.upper[j])) {
              merge_requests.push_back({bounds.middle, bounds.lower[i]});
              did_constrain = true;
            }
          }
        }
        if (not did_constrain) {
          if (bounds.lower.size() == 1) {
            merge_requests.push_back({bounds.middle, bounds.lower[0]});
          } else if (bounds.upper.size() == 1) {
            merge_requests.push_back({bounds.middle, bounds.upper[0]});
          } else {
            // TODO: IDK WHAT TO DO HERE
            // todo();
          }
        }
      }

      os << "Merge requests:\n";
      for (auto [a_id, b_id] : merge_requests) {
        log(os, a_id);
        os << " into ";
        log(os, b_id);
        os << '\n';
      }

      for (auto [a_id, b_id] : merge_requests) {
        if (std::holds_alternative<type::Variable>(ts.read(a_id))) {
          os << "Merging ";
          log(os, a_id);
          os << " into ";
          log(os, b_id);
          os << '\n';
          ts.merge_into(id::VariableId{a_id}, b_id);
        } else if (not ts.equal(a_id, b_id)) {
          // Should be unreachable. Try proving that.
          todo();
        }
      }
    }
  }

  std::deque<SubtypeOf> m_constraints;
};

} // namespace constraint
