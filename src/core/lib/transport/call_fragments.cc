// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/call_fragments.h"

namespace grpc_core {

FragmentAllocator::Node* FragmentAllocator::AllocateNode() {
  if (free_list_ != nullptr) {
    Node* node = free_list_;
    free_list_ = free_list_->next_free;
    return node;
  }
  return static_cast<Node*>(GetContext<Arena>()->Alloc(sizeof(Node)));
}

void FragmentAllocator::FreeNode(Node* node) {
  node->next_free = free_list_;
  free_list_ = node;
}

void FragmentAllocator::Delete(grpc_metadata_batch* p) {
  p->~grpc_metadata_batch();
  FreeNode(reinterpret_cast<Node*>(p));
}

void FragmentAllocator::Delete(Message* m) {
  m->~Message();
  FreeNode(reinterpret_cast<Node*>(m));
}

}  // namespace grpc_core
