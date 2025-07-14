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

#include "absl/log/check.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

namespace mpscpipe_detail {

// Multi Producer Single Consumer (MPSC) inter-activity communications.
// MPSC is used to communicate in between two or more Activities or Promise
// Parties in a thread safe way.
// The communication consists of one or more MpscSender objects and one
// MpscReceiver.

// Base MPSC class.
//
// The templates below wrap this and provide a more user friendly API.
// This class provides queuing of nodes, blocking those sends if there are too
// many tokens in the queue, a way of dequeuing nodes in order, and a way of
// lazily returning tokens to the queue after a node is dequeued.
//
// Notes:
//
// We split the queue in two.
//
// The first is an unbounded MPSC that we queue all prospective nodes into.
// Since the nodes exist, there's no reason not to keep track of them, but we
// do signal that nodes above the max_queued_ are blocked and refused to
// complete the send promise until they become accepted. The unbounded mpsc
// is derived from the same algorithm in mpscq.h, and consequently from
// http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
//
// That implementation is modified to allow a Waker object to be CAS-d in
// whenever we'd otherwise return nullptr to signify no ready node yet. This
// allows us to then wake up the stalled actor using the Activity wakeup system
// without needing any backoff/manual retry loop that's traditionally plagued
// usage of mpscq.h. If there are bugs, it's probably in those modifications.
//
// The second queue is an spsc queue that tracks nodes that have been accepted,
// that is that could be returned by Next() but have not yet been returned.
// We cheat a little here and allow (max_queued_ - 1) + (one node) to be
// accepted because it makes the logic much simpler - by decoupling the
// acceptance check and the dequeue operation.
//
// The second queue is entirely maintained by the single consumer.
class Mpsc {
 public:
  explicit Mpsc(size_t max_queued) : max_queued_(max_queued) {}
  ~Mpsc();

  // Base class for nodes in the queue.
  // Center<T>::Node extends this for various types.
  class Node {
   public:
    // One ref for blocking, one ref for releasing tokens. If there are no
    // tokens this is an immediate send, and so we don't need the ref for
    // blocking.
    explicit Node(uint32_t tokens) : tokens_(tokens) {}
    virtual ~Node() = default;

    uint32_t tokens() const { return tokens_; }

   private:
    friend class Mpsc;
    static constexpr uintptr_t kWakerPtr = 1;
    static constexpr uint8_t kBlockedState = 128;
    static constexpr uint8_t kClosedState = 64;
    static constexpr uint8_t kRefMask = 3;

    void Unref() {
      if ((state_.fetch_sub(1, std::memory_order_acq_rel) & kRefMask) == 1) {
        delete this;
      }
    }

    channelz::PropertyList ChannelzProperties() const {
      auto state = state_.load(std::memory_order_relaxed);
      return channelz::PropertyList()
          .Set("blocked", state & Node::kBlockedState)
          .Set("closed", state & Node::kClosedState)
          .Set("refs", state & Node::kRefMask);
    }

    const uint32_t tokens_;
    // All following fields are maintained by the Mpsc class.
    std::atomic<uint8_t> state_;
    Waker waker_;
    union {
      std::atomic<uintptr_t> next_{0};
      Node* spsc_next_;
    };
  };

 private:
  class SendPoller {
   public:
    explicit SendPoller(Node* node) : node_(node) {}
    ~SendPoller() {
      if (node_ != nullptr) node_->Unref();
    }
    SendPoller(const SendPoller&) = delete;
    SendPoller& operator=(const SendPoller&) = delete;
    SendPoller(SendPoller&& other) noexcept
        : node_(std::exchange(other.node_, nullptr)) {}
    SendPoller& operator=(SendPoller&& other) noexcept {
      std::swap(node_, other.node_);
      return *this;
    }
    Poll<StatusFlag> operator()() {
      auto state = node_->state_.load(std::memory_order_relaxed);
      if (state & Node::kClosedState) {
        node_->Unref();
        node_ = nullptr;
        return Failure{};
      }
      if (state & Node::kBlockedState) {
        return Pending{};
      }
      node_->Unref();
      node_ = nullptr;
      return Success{};
    }

    channelz::PropertyList ChannelzProperties() const {
      return node_->ChannelzProperties();
    }

   private:
    Node* node_;
  };

  class NextPoller {
   public:
    explicit NextPoller(Mpsc* mpsc) : mpsc_(mpsc) {}
    NextPoller(const NextPoller&) = delete;
    NextPoller& operator=(const NextPoller&) = delete;
    NextPoller(NextPoller&& other) noexcept
        : mpsc_(std::exchange(other.mpsc_, nullptr)) {}
    NextPoller& operator=(NextPoller&& other) noexcept {
      std::swap(mpsc_, other.mpsc_);
      return *this;
    }
    Poll<ValueOrFailure<Node*>> operator()() { return mpsc_->PollNext(); }
    channelz::PropertyList ChannelzProperties() const {
      return mpsc_->PollNextChannelzProperties();
    }

   private:
    Mpsc* mpsc_;
  };

 public:
  auto Send(Node* node) {
    DCHECK(node->waker_.is_unwakeable());
    // Enqueue the node immediately; this means that Send() must be called
    // from the same activity that will poll the result.
    Enqueue(node);
    return SendPoller(node);
  }

  StatusFlag UnbufferedImmediateSend(Node* node);

  auto Next() { return NextPoller(this); }
  Node* ImmediateNext() {
    Node* accepted_head = accepted_head_;
    if (accepted_head != nullptr) {
      accepted_head_ = reinterpret_cast<Node*>(
          accepted_head->next_.load(std::memory_order_relaxed));
    }
    return accepted_head;
  }
  void ReleaseTokens(Node* node);

  void Close(bool wake_reader);

  channelz::PropertyList ChannelzProperties() const;

  uint64_t QueuedTokens() const { return queued_tokens_.load(); }

 private:
  void Enqueue(Node* node);
  void ReleaseTokensAndClose(Node* node);
  Poll<Node*> Dequeue();
  Node* DequeueImmediate();
  Node* DequeueForDrain();
  // Returns true if we can accept more nodes.
  bool AcceptNode(Node* node);
  // Returns true if we can accept more nodes.
  // If it returns false, ensures a waker is set for the next enqueue.
  bool CheckActiveTokens();
  void DrainMpsc();
  void PushStub();
  void ReleaseActiveTokens(bool wake_reader, uint64_t tokens);
  Poll<ValueOrFailure<Node*>> PollNext();
  channelz::PropertyList PollNextChannelzProperties() const;

  // Top two bits of active tokens is used for synchronization of
  // the waker.
  // When we see we need to pause because active tokens exceeds max queued,
  // we first store a waker in active_tokens_waker_, then set active_tokens_ to
  // hold kActiveTokensWakerBit.
  // A token releaser to see active tokens less than max queued whilst the waker
  // bit is set will try to transition it to having just the /waking/ bit set.
  // If it succeeds, it holds the waker lock, and moves the waker out to a local
  // variable.
  // Next, it clears the top bits of active tokens, to indicate a waker can once
  // again be installed.
  // Finally, it executes the wakeup it's stored locally.
  static constexpr uint64_t kActiveTokensWakerBit = 1ull << 63;
  static constexpr uint64_t kActiveTokensWakingBit = 1ull << 62;
  static constexpr uint64_t kActiveTokensMask = kActiveTokensWakingBit - 1;
  const uint64_t max_queued_;

  // Each active enqueue is one actor, and the reader is an actor.
  // Once we close, we drop the reader actor, allowing each enqueuer to see
  // if it's the last enqueuer.
  // The last actor must drain the queue.
  std::atomic<uint64_t> actors_{1};
  std::atomic<uint64_t> queued_tokens_{0};
  Waker active_tokens_waker_;
  std::atomic<Node*> head_ = &stub_;
  alignas(GPR_CACHELINE_SIZE) Node* tail_ = &stub_;
  Node* accepted_head_ = nullptr;
  std::atomic<uint64_t> active_tokens_{0};
  Node stub_{0};
#ifndef NDEBUG
  bool drained = false;
#endif
};

// "Center" of the communication pipe.
// Contains sent but not received messages, and open/close state.
template <typename T>
class Center : public RefCounted<Center<T>, NonPolymorphicRefCount> {
 private:
  struct Node final : public Mpsc::Node {
    explicit Node(uint32_t tokens, T value)
        : Mpsc::Node(tokens), value(std::move(value)) {}
    T value;
  };

 public:
  // Construct the center with a maximum queue size.
  explicit Center(size_t max_queued) : mpsc_(max_queued) {}

  ~Center() {}

  class Queued {
   public:
    Queued() : node_(nullptr) {}
    Queued(Node* node, RefCountedPtr<Center<T>> center)
        : node_(node), center_(std::move(center)) {}
    ~Queued() {
      if (node_ != nullptr) center_->mpsc_.ReleaseTokens(node_);
    }
    Queued(const Queued&) = delete;
    Queued& operator=(const Queued&) = delete;
    Queued(Queued&& q) noexcept
        : node_(std::exchange(q.node_, nullptr)),
          center_(std::move(q.center_)) {}
    Queued& operator=(Queued&& q) noexcept {
      std::swap(node_, q.node_);
      std::swap(center_, q.center_);
      return *this;
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Queued& q) {
      absl::Format(&sink, "Queued{%s, tokens=%d}", absl::StrCat(q.node_->value),
                   q.node_->tokens());
    }

    friend std::ostream& operator<<(std::ostream& os, const Queued& q) {
      return os << absl::StrCat(q);
    }

    T& operator*() { return node_->value; }
    const T& operator*() const { return node_->value; }
    T* operator->() { return &node_->value; }
    const T* operator->() const { return &node_->value; }

    uint32_t tokens() const { return node_->tokens(); }

   private:
    Node* node_;
    RefCountedPtr<Center<T>> center_;
  };

  auto Send(T value, uint32_t tokens) {
    return mpsc_.Send(new Node(tokens, std::move(value)));
  }

  StatusFlag UnbufferedImmediateSend(T value, uint32_t tokens) {
    return mpsc_.UnbufferedImmediateSend(new Node(tokens, std::move(value)));
  }

  auto Next() {
    return Map(mpsc_.Next(),
               [this](ValueOrFailure<Mpsc::Node*> x) -> ValueOrFailure<Queued> {
                 if (!x.ok()) return Failure{};
                 return Queued(DownCast<Node*>(*x), this->Ref());
               });
  }

  auto NextBatch(size_t max_batch_size) {
    // Does not support delayed returning of tokens.
    return Map(mpsc_.Next(),
               [this, max_batch_size](ValueOrFailure<Mpsc::Node*> x)
                   -> ValueOrFailure<std::vector<T>> {
                 if (!x.ok()) return Failure{};
                 std::vector<T> result;
                 result.emplace_back(std::move(DownCast<Node*>(*x)->value));
                 mpsc_.ReleaseTokens(*x);
                 while (result.size() < max_batch_size) {
                   auto next = mpsc_.ImmediateNext();
                   if (next == nullptr) break;
                   result.emplace_back(std::move(DownCast<Node*>(next)->value));
                   mpsc_.ReleaseTokens(next);
                 }
                 return std::move(result);
               });
  }

  void ReceiverClosed(bool wake_reader) { mpsc_.Close(wake_reader); }

  uint64_t QueuedTokens() const { return mpsc_.QueuedTokens(); }

  channelz::PropertyList ChannelzProperties() const {
    return mpsc_.ChannelzProperties();
  }

 private:
  Mpsc mpsc_;
};

}  // namespace mpscpipe_detail

template <typename T>
using MpscQueued = typename mpscpipe_detail::Center<T>::Queued;

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
    return Map(center_->Send(std::move(t), tokens),
               [c = center_](auto x) { return x; });
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

template <typename T>
class MpscDebug {
 public:
  MpscDebug() = default;
  channelz::PropertyList ChannelzProperties() const {
    return center_->ChannelzProperties();
  }

 private:
  friend class MpscReceiver<T>;
  explicit MpscDebug(RefCountedPtr<mpscpipe_detail::Center<T>> center)
      : center_(std::move(center)) {}
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

template <typename T>
class MpscProbe {
 public:
  MpscProbe() = default;

  uint64_t QueuedTokens() const {
    return center_ == nullptr ? 0 : center_->QueuedTokens();
  }

 private:
  friend class MpscReceiver<T>;
  explicit MpscProbe(RefCountedPtr<mpscpipe_detail::Center<T>> center)
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
  MpscReceiver(MpscReceiver&& other) noexcept
      : center_(std::move(other.center_)) {}
  MpscReceiver& operator=(MpscReceiver&& other) noexcept {
    center_ = std::move(other.center_);
    return *this;
  }

  // Construct a new sender for this receiver. One receiver can have multiple
  // senders.
  MpscSender<T> MakeSender() { return MpscSender<T>(center_); }

  MpscDebug<T> MakeDebug() { return MpscDebug<T>(center_); }
  MpscProbe<T> MakeProbe() { return MpscProbe<T>(center_); }

  // Returns a promise that will resolve to ValueOrFailure<T>.
  // If receiving is closed, the promise will resolve to failure.
  // Otherwise, the promise resolves to the next item and removes
  // said item from the queue.
  auto Next() { return center_->Next(); }

  auto NextBatch(size_t max_batch_size) {
    return center_->NextBatch(max_batch_size);
  }

 private:
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MPSC_H
