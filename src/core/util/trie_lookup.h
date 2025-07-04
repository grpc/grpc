//
// Copyright 2025 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_UTIL_TRIE_LOOKUP_H
#define GRPC_SRC_CORE_UTIL_TRIE_LOOKUP_H

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace grpc_core {

template <typename Value>
class TrieLookupTree {
 public:
  TrieLookupTree() : root_(std::make_unique<TrieNode>()) {}

  // Takes value by r-value reference to consume it.
  bool AddNode(absl::string_view key, Value value,
               bool allow_overwrite = true) {
    auto* node = root_.get();
    for (auto c : key) {
      auto& child = node->child[c];
      if (child == nullptr) {
        child = std::make_unique<TrieNode>();
      }
      node = child.get();
    }
    if (node->value.has_value() && !allow_overwrite) {
      return false;
    }
    node->value = std::move(value);
    return true;
  }

  // Const-overload for lookups. Returns a const pointer to the value.
  const Value* Lookup(absl::string_view key) const {
    const auto* node = root_.get();
    for (auto c : key) {
      auto it = node->child.find(c);
      if (it == node->child.end()) {
        return nullptr;
      }
      node = it->second.get();
    }
    if (node->value.has_value()) {
      return &node->value.value();
    }
    return nullptr;
  }

  const Value* LookupLongestPrefix(absl::string_view key) const {
    const auto* node = root_.get();
    const Value* matched_value = nullptr;
    if (node->value.has_value()) {
      matched_value = &node->value.value();
    }
    for (auto c : key) {
      auto it = node->child.find(c);
      if (it == node->child.end()) {
        return matched_value;
      }
      node = it->second.get();
      if (node->value.has_value()) {
        matched_value = &node->value.value();
      }
    }
    return matched_value;
  }

  // Return all prefix matches 
  std::vector<const Value*> GetAllPrefixMatches(absl::string_view key) const {
    std::vector<const Value*> values;
    const auto* node = root_.get();
    for (auto c : key) {
      auto it = node->child.find(c);
      if (it == node->child.end()) {
        return values;
      }
      node = it->second.get();
      if (node->value.has_value()) {
        values.push_back(&node->value.value());
      }
    }
    return values;
  }

 private:
  struct TrieNode {
    absl::flat_hash_map<uint8_t, std::unique_ptr<TrieNode>> child;
    std::optional<Value> value;
  };
  std::unique_ptr<TrieNode> root_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_TRIE_LOOKUP_H