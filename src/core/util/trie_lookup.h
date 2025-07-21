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
  bool AddNode(absl::string_view key, Value value) {
    auto* node = root_.get();
    for (auto c : key) {
      auto& child = node->child[c];
      if (child == nullptr) {
        child = std::make_unique<TrieNode>();
      }
      node = child.get();
    }
    node->value = std::move(value);
    return true;
  }

  // Returns a const pointer to the value.
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
      return &*node->value;
    }
    return nullptr;
  }

  // Return longest matching prefix
  const Value* LookupLongestPrefix(absl::string_view key) const {
    const auto* node = root_.get();
    const Value* matched_value = nullptr;
    if (node->value.has_value()) {
      matched_value = &*node->value;
    }
    for (auto c : key) {
      auto it = node->child.find(c);
      if (it == node->child.end()) {
        return matched_value;
      }
      node = it->second.get();
      if (node->value.has_value()) {
        matched_value = &*node->value;
      }
    }
    return matched_value;
  }

  // Invokes cb for least to most matching prefix
  void ForEachPrefixMatch(absl::string_view key,
                          absl::FunctionRef<void(const Value&)> cb) const {
    const auto* node = root_.get();
    for (auto c : key) {
      auto it = node->child.find(c);
      if (it == node->child.end()) {
        return;
      }
      node = it->second.get();
      if (node->value.has_value()) {
        cb(*node->value);
      }
    }
  }

  // invokes cb for every value present in trie
  // Useful for ToString Method to dump Trie
  // Format of cb "void(key, value)"
  void ForEach(
      absl::FunctionRef<void(absl::string_view, const Value&)> cb) const {
    if (root_ == nullptr) return;
    std::string key;
    ForEachRecursive(root_.get(), key, cb);
  }

  // Check for equality
  bool operator==(const TrieLookupTree& other) const {
    if (this == &other) return true;
    if (root_ == nullptr || other.root_ == nullptr) {
      return root_ == other.root_;
    }
    std::vector<std::pair<const TrieNode*, const TrieNode*>> nodes_to_compare;
    nodes_to_compare.emplace_back(root_.get(), other.root_.get());
    while (!nodes_to_compare.empty()) {
      auto [node1, node2] = nodes_to_compare.back();
      nodes_to_compare.pop_back();
      if (node1->value.has_value() != node2->value.has_value()) {
        return false;
      }
      if (node1->value.has_value()) {
        if (!(*node1->value == *node2->value)) return false;
      }
      if (node1->child.size() != node2->child.size()) {
        return false;
      }
      for (const auto& [key, child1_ptr] : node1->child) {
        auto it = node2->child.find(key);
        if (it == node2->child.end()) {
          return false;
        }
        nodes_to_compare.emplace_back(child1_ptr.get(), it->second.get());
      }
    }
    return true;
  }
  bool operator!=(const TrieLookupTree& other) const {
    return !(*this == other);
  }

 private:
  struct TrieNode {
    absl::flat_hash_map<uint8_t, std::unique_ptr<TrieNode>> child;
    std::optional<Value> value;
  };

  void ForEachRecursive(
      const TrieNode* node, std::string& current_key,
      absl::FunctionRef<void(absl::string_view, const Value&)> cb) const {
    // check for value and invoke cb
    if (node->value.has_value()) {
      cb(current_key, *node->value);
    }
    // Recurse on childeren
    for (const auto& [character, child_node] : node->child) {
      current_key.push_back(character);
      ForEachRecursive(child_node.get(), current_key, cb);
      current_key.pop_back();
    }
  }

  std::unique_ptr<TrieNode> root_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_TRIE_LOOKUP_H
