module;

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

export module tag_storage;

import id;
import move_only_vector;
import tag;

export namespace tag_storage {

struct TagStorage {
  id::TagId get_tag(std::string_view name) {
    id::TagId const tag_id{m_tags.size()};

    auto [iter, did_insert] = m_tags.insert({name, tag_id});
    if (did_insert) {
      iter->second = tag_id;
    }
    return iter->second;
  }

  move_only_vector<tag::Tag> finalize() && {
    move_only_vector<tag::Tag> tags(m_tags.size());
    for (auto [name, tag_id] : m_tags) {
      tags[tag_id.value] = tag::Tag{.name = static_cast<std::string>(name)};
    }
    return tags;
  }

private:
  std::unordered_map<std::string_view, id::TagId> m_tags;
};

} // namespace tag_storage
