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

export struct Solver;

namespace {

// This works on the assumption that the syntax doesn't allow for applying anything other
// than references, type bindings, foralls, and other applications. Other applications are
// recursively resolved, type bindings should've been instantiated at this point because of
// SubtypeOfRules. That means this applies only references and partially-applied references.
[[nodiscard]] id::TypeId apply_type(type_storage::TypeStorage &ts,
                                    move_only_vector<entity::TypeDefinition> const &forms,
                                    id::TypeId function_id, id::TypeId argument_id) {
  if (auto *app = std::get_if<type::Application>(&ts.read(function_id))) {
    function_id = apply_type(ts, forms, app->function_id, app->argument_id);
  }
  if (auto *tt = std::get_if<type::TTLambda>(&ts.read(function_id))) {
    return ts.instantiate(tt->type_id, argument_id);
  }
  if (auto *forall = std::get_if<type::ForAll>(&ts.read(function_id))) {
    return ts.instantiate(forall->type_id, argument_id);
  }
  if (auto *ref = std::get_if<type::NamedTypeReference>(&ts.read(function_id))) {
    return apply_type(ts, forms, forms[ref->form_id.value].type_id, argument_id);
  }
  todo();
}

struct SubtypeOfRule {
  template <typename A, typename B> void operator()(A const &a, B const &b);

  move_only_vector<entity::TypeDefinition> const &forms;
  type_storage::TypeStorage &ts;
  Solver &solver;
  id::TypeId a_id;
  id::TypeId b_id;
};

std::size_t count(type_storage::TypeStorage const &ts, id::TypeId type_id, id::TypeId id) {
  struct Visitor {
    std::size_t operator()(type::Arrow arr) {
      return count(ts, arr.from_id, id) + count(ts, arr.to_id, id);
    }
    std::size_t operator()(type::ForAll forall) { return count(ts, forall.type_id, id); }
    std::size_t operator()(type::DeBruijnIndex) { return 0; }
    std::size_t operator()(type::Union const &v) {
      std::size_t cnt = 0;
      for (auto &e : v.elements) {
        cnt += count(ts, e.type_id, id);
      }
      return cnt;
    }
    std::size_t operator()(type::Struct const &s) {
      std::size_t cnt = 0;
      for (auto &e : s.elements) {
        cnt += count(ts, e.type_id, id);
      }
      return cnt;
    }
    std::size_t operator()(type::TTLambda tt_lambda) { return count(ts, tt_lambda.type_id, id); }
    std::size_t operator()(type::Application app) {
      return count(ts, app.function_id, id) + count(ts, app.argument_id, id);
    }
    std::size_t operator()(type::Variable const &) { return 0; }
    std::size_t operator()(type::RigidVariable const &) { return 0; }
    std::size_t operator()(type::NamedTypeReference const &) { return 0; }

    type_storage::TypeStorage const &ts;
    id::TypeId id;
  };

  if (ts.equal(type_id, id)) {
    return 1;
  }
  return std::visit(Visitor{ts, id}, ts.read(type_id));
}

} // namespace

struct Solver {
  Solver(type_storage::TypeStorage &ts) : ts(ts), m_constraints() {}

  void add_constraint(SubtypeOf c) {
    if (not ts.equal(c.a_id, c.b_id)) {
      m_constraints.push_back(c);
    }
  }

  // Example which loops forever:
  // form Int = Int
  // form Bool = Bool
  //
  // dec g :: Int -> Bool -> Int
  // def g = g
  //
  // def f x = g x x
  // TODO: The forms parameter can be removed if you somehow assert that all forms are supertypes of
  // their structure.
  void solve(std::ostream &os, std::function<void(std::ostream &os, id::TypeId)> log,
             move_only_vector<entity::TypeDefinition> const &forms) {
    while (not m_constraints.empty()) {
      // for (std::size_t i = 0; i < m_constraints.size(); ++i) {
      //   for (std::size_t j = i + 1; j < m_constraints.size(); ++j) {
      //     if (m_constraints[i].equal(ts, m_constraints[j])) {
      //       os << "Erasing (duplicate): ";
      //       m_constraints[i].log(os, log);
      //       os << '\n';
      //       m_constraints.erase(m_constraints.begin() + static_cast<std::ptrdiff_t>(j));
      //       --j;
      //     }
      //   }
      // }

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
            std::visit(SubtypeOfRule{forms, ts, *this, c.a_id, c.b_id}, a, b);
            did_something = true;
          }
        }

        if (did_something) {
          os << "Again...\n";
          continue;
        }
      }

      // At this point, every constraint contains at least 1 variable.
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
        for (std::size_t i = 0; i < bounds.lower.size(); ++i) {
          for (std::size_t j = 0; j < bounds.upper.size(); ++j) {
            if (ts.equal(bounds.lower[i], bounds.upper[j])) {
              merge_requests.push_back({bounds.middle, bounds.lower[i]});
            }
          }
        }
      }

      // Special case cuz idk how to solve this:
      // L <= #n where #n isn't mentioned anywhere else.
      if (merge_requests.empty()) {
        for (auto &[_, bounds] : var_bounds) {
          if (bounds.lower.size() == 1) {
            std::size_t cnt = 0;
            for (auto [a_id, b_id] : m_constraints) {
              cnt += count(ts, a_id, bounds.middle);
              cnt += count(ts, b_id, bounds.middle);
            }
            if (cnt == 1) {
              merge_requests.push_back({bounds.middle, bounds.lower[0]});
            }
          }
        }
      }

      // Special case cuz idk how to solve this:
      // #n <= U where #n isn't mentioned anywhere else.
      if (merge_requests.empty()) {
        for (auto &[_, bounds] : var_bounds) {
          if (bounds.upper.size() == 1) {
            std::size_t cnt = 0;
            for (auto [a_id, b_id] : m_constraints) {
              cnt += count(ts, a_id, bounds.middle);
              cnt += count(ts, b_id, bounds.middle);
            }
            if (cnt == 1) {
              merge_requests.push_back({bounds.middle, bounds.upper[0]});
            }
          }
        }
      }

      // Special case cuz idk how to solve this:
      // L <= #n <= U where #n isn't mentioned anywhere else.
      if (merge_requests.empty()) {
        for (auto &[_, bounds] : var_bounds) {
          if (bounds.lower.size() == 1 and bounds.upper.size() == 1) {
            std::size_t lcnt = 0, ucnt = 0;
            for (auto [a_id, b_id] : m_constraints) {
              lcnt += count(ts, a_id, bounds.middle);
              ucnt += count(ts, b_id, bounds.middle);
            }
            if (lcnt == 1 and ucnt == 1) {
              merge_requests.push_back({bounds.middle, bounds.lower[0]});
            }
          }
        }
      }

      if (merge_requests.empty()) {
        os << "Solving failed.\n";
        todo();
      }

      os << "Merge requests:\n";
      for (auto [a_id, b_id] : merge_requests) {
        log(os, a_id);
        os << " into ";
        log(os, b_id);
        os << '\n';
      }

      for (auto [a_id, b_id] : merge_requests) {
        if (auto var_id = ts.is_variable(a_id)) {
          os << "Merging ";
          log(os, a_id);
          os << " into ";
          log(os, b_id);
          os << '\n';
          ts.merge_into(*var_id, b_id);
        } else if (not ts.equal(a_id, b_id)) {
          // This is reached if you have something like:
          // x, y <= #n <= x, y
          // So, #n is merged first with one type, then the other.
          // If those two aren't equal, you're in much trouble.
          todo();
        }
      }

      for (std::size_t i = 0; i < m_constraints.size(); ++i) {
        auto [a_id, b_id] = m_constraints[i];
        if (ts.equal(a_id, b_id)) {
          m_constraints.erase(m_constraints.begin() + static_cast<std::ptrdiff_t>(i));
          --i;
        }
      }
    }
  }

private:
  type_storage::TypeStorage &ts;
  std::deque<SubtypeOf> m_constraints;
};

template <typename A, typename B> void SubtypeOfRule::operator()(A const &a, B const &b) {
  if constexpr (std::same_as<A, type::Arrow> and std::same_as<B, type::Arrow>) {
    solver.add_constraint(SubtypeOf{b.from_id, a.from_id});
    solver.add_constraint(SubtypeOf{a.to_id, b.to_id});
  } else if constexpr (std::same_as<A, type::ForAll> and std::same_as<B, type::ForAll>) {
    auto const a_type_id = a.type_id;
    auto const b_type_id = b.type_id;
    auto rigid_var = ts.make_rigid_variable();
    solver.add_constraint(SubtypeOf{
        ts.instantiate(a_type_id, rigid_var),
        ts.instantiate(b_type_id, rigid_var),
    });
  } else if constexpr (std::same_as<A, type::DeBruijnIndex> and
                       std::same_as<B, type::DeBruijnIndex>) {
    if (a.value != b.value) {
      todo();
    }
  } else if constexpr (std::same_as<A, type::ForAll> and std::same_as<B, type::Arrow>) {
    solver.add_constraint(SubtypeOf{ts.instantiate(a.type_id, ts.make_variable()), b_id});
  } else if constexpr (std::same_as<A, type::Union> and std::same_as<B, type::Union>) {
    for (auto &e1 : a.elements) {
      auto it = std::ranges::find(b.elements, e1.tag_id, &type::Element::tag_id);
      if (it == b.elements.end()) {
        todo();
      }
      solver.add_constraint(SubtypeOf{e1.type_id, it->type_id});
    }
  } else if constexpr (std::same_as<A, type::Struct> and std::same_as<B, type::Struct>) {
    for (auto &e2 : b.elements) {
      auto it = std::ranges::find(a.elements, e2.tag_id, &type::Element::tag_id);
      if (it == a.elements.end()) {
        todo();
      }
      solver.add_constraint(SubtypeOf{it->type_id, e2.type_id});
    }
  } else if constexpr (std::same_as<A, type::RigidVariable> and
                       std::same_as<B, type::RigidVariable>) {
    if (not ts.equal(a_id, b_id)) {
      todo();
    }
  } else if constexpr (std::same_as<A, type::NamedTypeReference> and
                       std::same_as<B, type::NamedTypeReference>) {
    if (a.form_id != b.form_id) {
      todo();
    }
  } else if constexpr (std::same_as<A, type::Application> and std::same_as<B, type::Application>) {
    solver.add_constraint(SubtypeOf{a.function_id, b.function_id});
    solver.add_constraint(SubtypeOf{a.argument_id, b.argument_id});
  } else if constexpr (std::same_as<B, type::NamedTypeReference>) {
    solver.add_constraint(SubtypeOf{a_id, forms[b.form_id.value].type_id});
  } else if constexpr (std::same_as<A, type::Application>) {
    solver.add_constraint(SubtypeOf{
        apply_type(ts, forms, a.function_id, a.argument_id),
        b_id,
    });
  } else if constexpr (std::same_as<B, type::Application>) {
    solver.add_constraint(SubtypeOf{
        a_id,
        apply_type(ts, forms, b.function_id, b.argument_id),
    });
  } else {
    // static_assert(false);
    todo();
  }
}

} // namespace constraint
