module;

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

export module scope;

import ast;
import entity;
import id;

export namespace scope {

struct TypeBinding {
  ast::kind::Kind const *kind;
  std::size_t absolute_index;
};

struct Binding {
  std::reference_wrapper<entity::Binding const> binding;
};

struct TypeDefinition {
  ast::kind::Kind const *kind;
  id::FormId form_id;
};

struct Value {
  id::ValueId value_id;
};

using EntryBase = std::variant<TypeBinding, Binding, TypeDefinition, Value>;

struct Entry : EntryBase {
  using EntryBase::EntryBase;
};

struct Scope {
  Scope(std::unordered_map<std::string_view, Entry> bindings, Scope const *parent)
      : m_entries(std::move(bindings)), m_parent(parent) {}

  std::optional<Entry> lookup(std::string_view name) const {
    Scope const *scope = this;
    while (true) {
      if (auto it = scope->m_entries.find(name); it != scope->m_entries.end()) {
        return it->second;
      }
      if (not scope->m_parent) {
        return std::nullopt;
      }
      scope = scope->m_parent;
    }
  }

  // void capture(entity::Binding const &binding) {
  //   Scope *scope = this;
  //   while (true) {
  //     for (auto &[_, entry] : scope->m_entries) {
  //       auto *binding_entry = std::get_if<scope::Binding>(&entry);
  //       if (binding_entry and binding_entry->binding_ptr == &binding) {
  //         return;
  //       }
  //     }
  //     if (not scope->m_parent) {
  //       // If this executes, something got screeeewed.
  //       // This means you're trying to capture a binding which is not in scope.
  //       std::unreachable();
  //     }
  //
  //     // FIX: err check
  //     scope->m_captures.insert(&binding);
  //     scope = scope->m_parent;
  //   }
  // }

  // std::unordered_set<entity::Binding const *> const &captures() const { return m_captures; }

private:
  std::unordered_map<std::string_view, Entry> m_entries;
  // std::unordered_set<entity::Binding const *> m_captures;
  Scope const *m_parent;
};

} // namespace scope
