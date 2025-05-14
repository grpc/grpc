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

#include "src/core/lib/promise/mpsc.h"

#include <atomic>
#include <cstdint>

#include "absl/log/check.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/util/sync.h"

namespace grpc_core::mpscpipe_detail {

void Mpsc::Close(bool wake_reader) {
  Node* dangling = nullptr;
  Waker read_waker;
  {
    // Lock everything, coalesce the queues, signal in the serial counters that
    // we are closed.
    MutexLock read_lock(&mu_read_);
    MutexLock queue_lock(&mu_queue_);
    pull_serial_.store(kClosedSerial, std::memory_order_relaxed);
    queue_serial_ = kClosedSerial;
    if (read_tail_ != nullptr) {
      DCHECK_EQ(read_tail_->next_, nullptr);
      read_tail_->next_ = queue_head_;
      dangling = read_head_;
    } else {
      DCHECK_EQ(read_head_, nullptr);
      dangling = queue_head_;
    }
    queue_head_ = nullptr;
    queue_tail_ = nullptr;
    block_head_ = nullptr;
    read_head_ = nullptr;
    read_tail_ = nullptr;
    pull_head_ = nullptr;
    if (wake_reader) read_waker = std::move(read_waker_);
  }
  // Wake up everything that was previously queued, and delete the nodes.
  while (dangling != nullptr) {
    auto* next = dangling->next_;
    dangling->waker_.Wakeup();
    delete dangling;
    dangling = next;
  }
  read_waker.Wakeup();
}

bool Mpsc::FetchNodesFromQueue() {
  MutexLock queue_lock(&mu_queue_);
  // Check for queue closure.
  if (queue_serial_ == kClosedSerial) return false;
  // If the queue is empty, record a waiter and return false.
  // We are blocked.
  if (queue_head_ == nullptr) {
    read_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return false;
  }
  if (read_head_ == nullptr) {
    DCHECK_EQ(read_tail_, nullptr);
    DCHECK_EQ(block_head_, nullptr);
    DCHECK_EQ(pull_head_, nullptr);
    read_head_ = queue_head_;
    block_head_ = queue_head_;
    pull_head_ = queue_head_;
  } else {
    DCHECK_NE(read_tail_, nullptr);
    read_tail_->next_ = queue_head_;
    if (pull_head_ == nullptr) pull_head_ = read_tail_->next_;
    if (block_head_ == nullptr) block_head_ = read_tail_->next_;
  }
  read_tail_ = queue_tail_;
  queue_head_ = nullptr;
  queue_tail_ = nullptr;
  return true;
}

uint64_t Mpsc::Enqueue(std::unique_ptr<Node> node, bool immediate) {
  mu_queue_.Lock();
  // Check for queue closure.
  if (queue_serial_ == kClosedSerial) {
    mu_queue_.Unlock();
    // node is deleted here.
    return kClosedSerial;
  }
  queue_serial_ += node->tokens_;
  const uint64_t queue_serial = queue_serial_;
  if (!immediate &&
      queue_serial_ - pull_serial_.load(std::memory_order_relaxed) >
          max_queued_) {
    // Need to track waker for blocking.
    node->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }
  // Remember the serial number at which this node became unblocked.
  node->unblocked_at_serial_ = queue_serial;
  node->next_ = nullptr;
  Node* released_node = node.release();
  if (queue_tail_ == nullptr) {
    DCHECK_EQ(queue_head_, nullptr);
    queue_head_ = released_node;
  } else {
    queue_tail_->next_ = released_node;
  }
  queue_tail_ = released_node;
  auto read_waker = std::move(read_waker_);
  mu_queue_.Unlock();
  read_waker.Wakeup();
  return queue_serial;
}

void Mpsc::ReleaseTokens(uint64_t tokens) {
  auto query = QueryUnblockedNodes(tokens);
  query.wakeup_nodes.ForEach([](Node* node) { node->waker_.Wakeup(); });
  query.delete_nodes.ForEach([](Node* node) { delete node; });
}

Mpsc::QueryUnblockedNodesResult Mpsc::QueryUnblockedNodes(
    uint64_t tokens_to_release) {
  MutexLock read_lock(&mu_read_);
  auto pull_serial = pull_serial_.load(std::memory_order_relaxed);
  // Check if the queue is closed.
  if (pull_serial == kClosedSerial) return {NodeRange(), NodeRange()};
  // Load/store pair is safe because we hold the read lock.
  // Avoids an extra RMW.
  pull_serial += tokens_to_release;
  pull_serial_.store(pull_serial, std::memory_order_relaxed);
  if (block_head_ == nullptr) {
    // Blocked nodes have reached the end of the read list... go grab some more.
    // If we cannot, then we are done - there were no nodes to unblock.
    if (!FetchNodesFromQueue()) return {NodeRange(), NodeRange()};
  }
  Node* begin = block_head_;
  while (true) {
    DCHECK_NE(block_head_, nullptr);
    // Check if the current head is still blocked - if so we're done.
    if (block_head_->unblocked_at_serial_ > pull_serial + max_queued_) break;
    block_head_ = block_head_->next_;
    if (block_head_ == nullptr) {
      // We've reached the end of the read list... go grab some more.
      // If we cannot, then we are done.
      if (!FetchNodesFromQueue()) break;
    }
  }
  // Return two ranges: the nodes we can unblock, and the nodes we can delete
  // because they've also been pulled.
  return {NodeRange(begin, block_head_), CatchUpReadHead()};
}

Mpsc::NodeRange Mpsc::CatchUpReadHead() {
  // We only need to track nodes up to either the pull head or the block head,
  // whichever is older. Nodes prior to that are now irrelevant and can be
  // deleted.
  Node* old_read_head = read_head_;
  while (read_head_ != pull_head_ && read_head_ != block_head_) {
    read_head_ = read_head_->next_;
  }
  if (read_head_ == nullptr) read_tail_ = nullptr;
  return NodeRange(old_read_head, read_head_);
}

}  // namespace grpc_core::mpscpipe_detail
