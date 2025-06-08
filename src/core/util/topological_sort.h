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

#ifndef GRPC_SRC_CORE_UTIL_TOPOLOGICAL_SORT_H
#define GRPC_SRC_CORE_UTIL_TOPOLOGICAL_SORT_H

#include <cstddef>

#include "manual_constructor.h"
#include "src/core/util/bitset.h"

namespace grpc_core {

namespace topological_sort_detail {}  // namespace topological_sort_detail

template <size_t kMaxNodes>
class TopologicalSort {
 public:
  explicit TopologicalSort(size_t num_nodes) : num_nodes_(num_nodes) {
    for (size_t i = 0; i < num_nodes; ++i) {
      nodes_before_[i] = 0;
      nodes_after_[i].Init();
      ready_nodes_.Set(i, true);
    }
  }

  void AddDependency(size_t from, size_t to) {
    if (nodes_after_[from]->is_set(to)) return;
    nodes_after_[from]->set(to);
    ++nodes_before_[to];
    ready_nodes_.clear(to);
  }

  // output is called with each node index in the sort, in order
  // returns true on success, false on failure
  template <typename Fn>
  bool Sort(Fn output) {
    for (size_t i = 0; i < num_nodes_; i++) {
      size_t next_node;
      if (GPR_UNLIKELY(!ready_nodes_.LowestBitSet(next_node))) {
        return false;
      }
      output(next_node);
      ready_nodes_.clear(next_node);
      nodes_after_[next_node]->ForEachBitSet([this](size_t i) {
        if (--nodes_before_[i] == 0) ready_nodes_.set(i);
      });
    }
    DCHECK(!ready_nodes_.any());
    return true;
  }

 private:
  UintWithMax<kMaxNodes> num_nodes_ = 0;
  UintWithMax<kMaxNodes> nodes_before_[kMaxNodes];
  ManualConstructor<BitSet<kMaxNodes>> nodes_after_[kMaxNodes];
  BitSet<kMaxNodes> ready_nodes_;
};

}  // namespace grpc_core

#endif
