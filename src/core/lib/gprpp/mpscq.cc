//
//
// Copyright 2016 gRPC authors.
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
//

#include "src/core/lib/gprpp/mpscq.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

//
// MultiProducerSingleConsumerQueue
//

bool MultiProducerSingleConsumerQueue::Push(Node* node) {
  node->next.store(nullptr, std::memory_order_relaxed);
  Node* prev = head_.exchange(node, std::memory_order_acq_rel);
  prev->next.store(node, std::memory_order_release);
  return prev == &stub_;
}

MultiProducerSingleConsumerQueue::Node*
MultiProducerSingleConsumerQueue::Pop() {
  bool empty;
  return PopAndCheckEnd(&empty);
}

MultiProducerSingleConsumerQueue::Node*
MultiProducerSingleConsumerQueue::PopAndCheckEnd(bool* empty) {
  Node* tail = tail_;
  Node* next = tail_->next.load(std::memory_order_acquire);
  if (tail == &stub_) {
    // indicates the list is actually (ephemerally) empty
    if (next == nullptr) {
      *empty = true;
      return nullptr;
    }
    tail_ = next;
    tail = next;
    next = tail->next.load(std::memory_order_acquire);
  }
  if (next != nullptr) {
    *empty = false;
    tail_ = next;
    return tail;
  }
  Node* head = head_.load(std::memory_order_acquire);
  if (tail != head) {
    *empty = false;
    // indicates a retry is in order: we're still adding
    return nullptr;
  }
  Push(&stub_);
  next = tail->next.load(std::memory_order_acquire);
  if (next != nullptr) {
    *empty = false;
    tail_ = next;
    return tail;
  }
  // indicates a retry is in order: we're still adding
  *empty = false;
  return nullptr;
}

//
// LockedMultiProducerSingleConsumerQueue
//

bool LockedMultiProducerSingleConsumerQueue::Push(Node* node) {
  return queue_.Push(node);
}

LockedMultiProducerSingleConsumerQueue::Node*
LockedMultiProducerSingleConsumerQueue::TryPop() {
  if (mu_.TryLock()) {
    Node* node = queue_.Pop();
    mu_.Unlock();
    return node;
  }
  return nullptr;
}

LockedMultiProducerSingleConsumerQueue::Node*
LockedMultiProducerSingleConsumerQueue::Pop() {
  MutexLock lock(&mu_);
  bool empty = false;
  Node* node;
  do {
    node = queue_.PopAndCheckEnd(&empty);
  } while (node == nullptr && !empty);
  return node;
}

}  // namespace grpc_core
