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

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace grpc_core {

template <typename Value>
struct TrieNode {
  absl::flat_hash_map<uint8_t, std::unique_ptr<TrieNode>> child_;
  std::shared_ptr<Value> value;
};

template <typename Value>
class TrieLookupTree {
  std::unique_ptr<TrieNode<Value>> root_;

 public:
  TrieLookupTree() : root_(std::make_unique<TrieNode<Value>>()) {}
  bool addNode(absl::string_view key, std::shared_ptr<Value> value,
               bool allow_overwrite = true) {
    auto node = root_.get();
    for (auto c : key) {
      auto it = node->child_.find(c);
      if (it == node->child_.end()) {
        node->child_[c] = std::make_unique<TrieNode<Value>>();
      }
      node = node->child_[c].get();
    }
    if (node->value != nullptr && !allow_overwrite) {
      return false;
    }
    node->value = value;
    return true;
  }

  std::shared_ptr<Value> lookup(absl::string_view key) const {
    const auto* node = root_.get();
    for (auto c : key) {
      auto it = node->child_.find(c);
      if (it == node->child_.end()) return absl::nullopt;
      node = it->second.get();
    }
    return node->value;
  }

  // Return Longest matching prefix value
  std::shared_ptr<Value> lookupLongestPrefix(absl::string_view key) const {
    const auto* node = root_.get();
    std::shared_ptr<Value> matched_value = nullptr;
    for (auto c : key) {
      auto it = node->child_.find(c);
      if (it == node->child_.end()) return matched_value;
      node = it->second.get();
      if (node->value != nullptr) {
        matched_value = node->value;
      }
    }
    return matched_value;
  }
};

}  // namespace grpc_core
