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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_MPSC_H
#define GRPC_SRC_CORE_LIB_PROMISE_MPSC_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {

namespace mpscpipe_detail {

// Multi Producer Single Consumer (MPSC) inter-activity communications.
// MPSC is used to communicate in between two or more Activities or Promise
// Parties in a thread safe way.
// The communication consists of one or more MpscSender objects and one
// MpscReceiver.

// Implementation notes:
// We maintain two queues - one for the senders, one for the reader, each
// under its own mutex.
// Each sent item is enqueued with some number of tokens, and when the queue
// is over max_queued tokens sends become blocking.
// Receiving a node from Next() is not sufficient to release those tokens,
// instead a separate call to ReleaseTokens() is required. This allows
// receivers from the MPSC to delay the release of tokens until further
// processing is complete.
// The read mutex is then required to track the release of objects (which
// needs to happen after both the pull and the wakeup because of token
// release).
// When tokens are released, we wakeup blocked senders in the order they
// were enqueued.
// Note carefully: since receivers may return tokens in any order, it means
// that a different sent item may be unblocked than the one that caused the
// tokens to be released. Nevertheless, it's thought that this is fair.
class Mpsc {
 public:
  explicit Mpsc(size_t max_queued) : max_queued_(max_queued) {}

  // Base class for nodes in the queue.
  // Center<T>::Node extends this for various types.
  class Node {
   public:
    explicit Node(uint32_t tokens) : tokens_(tokens) {}
    virtual ~Node() = default;

    uint32_t tokens() const { return tokens_; }
    Node* next() const { return next_; }

   private:
    friend class Mpsc;
    const uint32_t tokens_;
    // All following fields are maintained by the Mpsc class.
    uint64_t unblocked_at_serial_;
    Waker waker_;
    Node* next_;
  };

  auto Send(std::unique_ptr<Node> node) {
    DCHECK(node->waker_.is_unwakeable());
    // Enqueue the node immediately; this means that Send() must be called
    // from the same activity that will poll the result.
    auto queue_serial = Enqueue(std::move(node), false);
    return [queue_serial, this]() -> Poll<StatusFlag> {
      if (queue_serial == kClosedSerial) return Failure{};
      if (queue_serial <=
          pull_serial_.load(std::memory_order_relaxed) + max_queued_) {
        return Success{};
      }
      return Pending{};
    };
  }

  StatusFlag UnbufferedImmediateSend(std::unique_ptr<Node> node) {
    DCHECK(node->waker_.is_unwakeable());
    return StatusFlag(Enqueue(std::move(node), true) != kClosedSerial);
  }

  template <typename F>
  auto Next(F map) {
    using Result = decltype(map(nullptr).first);
    return [this, map = std::move(map)]() -> Poll<ValueOrFailure<Result>> {
      mu_read_.Lock();
      // If the pull list is empty, we need to fetch some more from the
      // enqueue list.
      if (pull_head_ == nullptr && !FetchNodesFromQueue()) {
        if (GPR_UNLIKELY(pull_serial_.load(std::memory_order_relaxed) ==
                         kClosedSerial)) {
          mu_read_.Unlock();
          return Failure{};
        }
        mu_read_.Unlock();
        return Pending{};
      }
      DCHECK_NE(pull_head_, nullptr);
      auto* node = pull_head_;
      auto [result, pull_head] = map(node);
      pull_head_ = pull_head;
      NodeRange delete_nodes = CatchUpReadHead();
      mu_read_.Unlock();
      delete_nodes.ForEach([](Node* node) { delete node; });
      return std::move(result);
    };
  }

  void ReleaseTokens(uint64_t tokens);
  void Close(bool wake_reader);

 private:
  // Helper class for capturing a range of nodes in a linked list and
  // iterating over them - we need to do this in a few places to keep
  // node deletion outside of locks.
  class NodeRange {
   public:
    NodeRange() = default;
    NodeRange(Node* first, Node* last) : first_(first), last_(last) {}

    template <typename F>
    void ForEach(F f) {
      Node* node = first_;
      while (node != last_) {
        Node* next = node->next_;
        f(node);
        node = next;
      }
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const NodeRange& range) {
      absl::Format(&sink, "%p..%p", range.first_, range.last_);
    }

   private:
    Node* first_ = nullptr;
    Node* last_ = nullptr;
  };

  struct QueryUnblockedNodesResult {
    NodeRange wakeup_nodes;
    NodeRange delete_nodes;
  };

  bool FetchNodesFromQueue() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_read_)
      ABSL_LOCKS_EXCLUDED(mu_queue_);
  uint64_t Enqueue(std::unique_ptr<Node> node, bool immediate)
      ABSL_LOCKS_EXCLUDED(mu_queue_, mu_read_);
  QueryUnblockedNodesResult QueryUnblockedNodes(uint64_t tokens_to_release)
      ABSL_LOCKS_EXCLUDED(mu_queue_, mu_read_);
  Poll<ValueOrFailure<Node*>> PollPull() ABSL_LOCKS_EXCLUDED(mu_read_);
  NodeRange CatchUpReadHead() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_read_);

  static constexpr uint64_t kClosedSerial =
      std::numeric_limits<uint64_t>::max();
  const uint64_t max_queued_;
  // Enqueue state: protected by a mutex because we need all of this to be in
  // sync.
  // TODO(ctiller): there's likely a way to make this lock free. Investigate if
  // it's better. When doing so, ensure that queue_serial increments are in
  // order with node insertion order so we can safely unblock.
  Mutex mu_queue_;
  // Serial number of the queue. This is the number of tokens that have been
  // pushed in the lifetime of the queue.
  uint64_t queue_serial_ ABSL_GUARDED_BY(mu_queue_) = 0;
  Node* queue_head_ ABSL_GUARDED_BY(mu_queue_) = nullptr;
  Node* queue_tail_ ABSL_GUARDED_BY(mu_queue_) = nullptr;
  // Serial number of the last pulled item. The difference between queue_serial_
  // and pull_serial_ is the number of tokens that are currently in the queue.
  std::atomic<uint64_t> pull_serial_{0};
  alignas(GPR_CACHELINE_SIZE) Mutex mu_read_ ABSL_ACQUIRED_BEFORE(mu_queue_);
  // Read queue is arranged into three pointers into the same list (and a single
  // tail).
  // The oldest node is read_head_, and the newest is read_tail_.
  // pull_head_ is either the same or after read_head_, and points to the next
  // node to be pulled from the queue.
  // block_head_ is either the same or after read_tail_, and points to the next
  // node that is blocked on queue size.
  Node* pull_head_ ABSL_GUARDED_BY(mu_read_) = nullptr;
  Node* block_head_ ABSL_GUARDED_BY(mu_read_) = nullptr;
  Node* read_head_ ABSL_GUARDED_BY(mu_read_) = nullptr;
  Node* read_tail_ ABSL_GUARDED_BY(mu_read_) = nullptr;
  Waker read_waker_ ABSL_GUARDED_BY(mu_queue_);
};

// "Center" of the communication pipe.
// Contains sent but not received messages, and open/close state.
template <typename T>
class Center : public RefCounted<Center<T>, NonPolymorphicRefCount> {
 public:
  // Construct the center with a maximum queue size.
  explicit Center(size_t max_queued) : mpsc_(max_queued) {}

  ~Center() {}

  template <typename V>
  class Queued {
   public:
    Queued() : tokens_(0) {}
    Queued(V value, uint32_t tokens, RefCountedPtr<Center<T>> center)
        : value_(std::move(value)),
          tokens_(tokens),
          center_(std::move(center)) {}
    ~Queued() {
      if (tokens_ > 0) center_->ReleaseTokens(tokens_);
    }
    Queued(const Queued&) = delete;
    Queued& operator=(const Queued&) = delete;
    Queued(Queued&& q)
        : value_(std::move(q.value_)),
          tokens_(std::exchange(q.tokens_, 0)),
          center_(std::move(q.center_)) {}
    Queued& operator=(Queued&& q) = delete;

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Queued& q) {
      absl::Format(&sink, "Queued{%s, tokens=%d}", absl::StrCat(q.value_),
                   q.tokens_);
    }

    friend std::ostream& operator<<(std::ostream& os, const Queued& q) {
      return os << absl::StrCat(q);
    }

    V& operator*() { return value_; }
    const V& operator*() const { return value_; }
    V* operator->() { return &value_; }
    const V* operator->() const { return &value_; }

    uint32_t tokens() const { return tokens_; }

   private:
    V value_;
    uint32_t tokens_;
    RefCountedPtr<Center<T>> center_;
  };

  auto Send(T value, uint32_t tokens) {
    return mpsc_.Send(std::make_unique<Node>(tokens, std::move(value)));
  }

  StatusFlag UnbufferedImmediateSend(T value, uint32_t tokens) {
    return mpsc_.UnbufferedImmediateSend(
        std::make_unique<Node>(tokens, std::move(value)));
  }

  auto Next() {
    return mpsc_.Next([this](Mpsc::Node* n) {
      return std::pair(Queued<T>(std::move(DownCast<Node*>(n)->value),
                                 n->tokens(), this->Ref()),
                       n->next());
    });
  }

  auto AllNext() {
    return mpsc_.Next([this](Mpsc::Node* n) {
      std::vector<T> result;
      uint64_t tokens = 0;
      while (n != nullptr) {
        tokens += n->tokens();
        result.emplace_back(std::move(DownCast<Node*>(n)->value));
        n = n->next();
      }
      return std::pair(
          Queued<std::vector<T>>(std::move(result), tokens, this->Ref()),
          nullptr);
    });
  }

  void ReleaseTokens(uint64_t tokens) { mpsc_.ReleaseTokens(tokens); }

  void ReceiverClosed(bool wake_reader) { mpsc_.Close(wake_reader); }

 private:
  struct Node final : public Mpsc::Node {
    explicit Node(uint32_t tokens, T value)
        : Mpsc::Node(tokens), value(std::move(value)) {}
    T value;
  };

  Mpsc mpsc_;
};

}  // namespace mpscpipe_detail

template <typename T>
using MpscQueued = typename mpscpipe_detail::Center<T>::template Queued<T>;
template <typename T>
using MpscQueuedVec =
    typename mpscpipe_detail::Center<T>::template Queued<std::vector<T>>;

template <typename T>
class MpscReceiver;

// Send half of an mpsc pipe.
template <typename T>
class MpscSender {
 public:
  MpscSender() = default;
  MpscSender(const MpscSender&) = default;
  MpscSender& operator=(const MpscSender&) = default;
  MpscSender(MpscSender&&) noexcept = default;
  MpscSender& operator=(MpscSender&&) noexcept = default;

  // Input: Input is the object that you want to send. The promise that is
  // returned by Send will take ownership of the object.
  // Return: Returns a promise that will send one item.
  // This promise can either return
  // 1. Pending{} if the sending is still pending
  // 2. Resolves to true if sending is successful
  // 3. Resolves to false if the receiver was closed and the value
  //    will never be successfully sent.
  // The promise returned is thread safe. We can use multiple send calls
  // in parallel to generate multiple such send promises and these promises can
  // be run in parallel in a thread safe way.
  auto Send(T t, uint32_t tokens) {
    return center_->Send(std::move(t), tokens);
  }

  StatusFlag UnbufferedImmediateSend(T t, uint32_t tokens) {
    return center_->UnbufferedImmediateSend(std::move(t), tokens);
  }

 private:
  friend class MpscReceiver<T>;
  explicit MpscSender(RefCountedPtr<mpscpipe_detail::Center<T>> center)
      : center_(std::move(center)) {}
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

// Receive half of an mpsc pipe.
template <typename T>
class MpscReceiver {
 public:
  // max_buffer_hint is the maximum number of tokens we'd like to buffer.
  explicit MpscReceiver(uint64_t max_buffer_hint)
      : center_(MakeRefCounted<mpscpipe_detail::Center<T>>(max_buffer_hint)) {}
  ~MpscReceiver() {
    if (center_ != nullptr) center_->ReceiverClosed(false);
  }
  // Marking the receiver closed will make sure it will not receive any
  // messages. If a sender tries to Send a message to a closed receiver,
  // sending will fail.
  void MarkClosed() {
    if (center_ != nullptr) center_->ReceiverClosed(true);
  }
  MpscReceiver(const MpscReceiver&) = delete;
  MpscReceiver& operator=(const MpscReceiver&) = delete;
  // Only movable until it's first polled, and so we don't need to contend with
  // a non-empty buffer during a legal move!
  MpscReceiver(MpscReceiver&& other) noexcept
      : center_(std::move(other.center_)) {
    DCHECK(other.buffer_.empty());
  }
  MpscReceiver& operator=(MpscReceiver&& other) noexcept {
    DCHECK(other.buffer_.empty());
    center_ = std::move(other.center_);
    return *this;
  }

  // Construct a new sender for this receiver. One receiver can have multiple
  // senders.
  MpscSender<T> MakeSender() { return MpscSender<T>(center_); }

  // Returns a promise that will resolve to ValueOrFailure<T>.
  // If receiving is closed, the promise will resolve to failure.
  // Otherwise, the promise resolves to the next item and removes
  // said item from the queue.
  auto Next() { return center_->Next(); }

  auto AllNext() { return center_->AllNext(); }

 private:
  // Received items. We move out of here one by one, but don't resize the
  // vector. Instead, when we run out of items, we poll the center for more -
  // which swaps this buffer in for the new receive queue and clears it.
  // In this way, upon hitting a steady state the queue ought to be allocation
  // free.
  std::vector<T> buffer_;
  typename std::vector<T>::iterator buffer_it_ = buffer_.end();
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MPSC_H
