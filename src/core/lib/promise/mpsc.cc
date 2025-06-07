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

Mpsc::~Mpsc() { Close(false); }

void Mpsc::Enqueue(Node* node) {
  GRPC_LATENT_SEE_SCOPE("Mpsc::Enqueue");
  auto actors = actors_.load(std::memory_order_relaxed);
  while (true) {
    if (actors == 0) {
      // Queue closed - node not yet added to queue.
      node->state_.store(1 /*ref*/ | Node::kClosedState,
                         std::memory_order_relaxed);
      return;
    }
    if (actors_.compare_exchange_weak(actors, actors + 1,
                                      std::memory_order_relaxed)) {
      break;
    }
  }
  const auto tokens = node->tokens_;
  if (tokens > 0 &&
      queued_tokens_.fetch_add(tokens, std::memory_order_relaxed) + tokens >
          max_queued_) {
    node->waker_ = GetContext<Activity>()->MakeOwningWaker();
    node->state_.store(2 /*refs*/ | Node::kBlockedState,
                       std::memory_order_release);
  } else {
    node->state_.store(2 /*refs*/, std::memory_order_release);
  }
  DCHECK_EQ(node->next_.load(std::memory_order_relaxed), 0u);
  Node* prev = head_.exchange(node, std::memory_order_acq_rel);
  uintptr_t prev_next = prev->next_.exchange(reinterpret_cast<uintptr_t>(node),
                                             std::memory_order_acq_rel);
  if (1 == actors_.fetch_sub(1, std::memory_order_acq_rel)) {
    DrainMpsc();
  }
  if (prev_next == 0) return;
  DCHECK_NE(prev_next & Node::kWakerPtr, 0u);
  Waker* waker = reinterpret_cast<Waker*>(prev_next & ~Node::kWakerPtr);
  DCHECK_NE(waker, nullptr);
  waker->Wakeup();
  delete waker;
}

StatusFlag Mpsc::UnbufferedImmediateSend(Node* node) {
  GRPC_LATENT_SEE_SCOPE("Mpsc::UnbufferedImmediateSend");
  auto actors = actors_.load(std::memory_order_relaxed);
  while (true) {
    if (actors == 0) {
      // Queue closed - node not yet added to queue.
      delete node;
      return Failure{};
    }
    if (actors_.compare_exchange_weak(actors, actors + 1,
                                      std::memory_order_relaxed)) {
      break;
    }
  }
  queued_tokens_.fetch_add(node->tokens_, std::memory_order_relaxed);
  node->state_.store(1, std::memory_order_relaxed);
  Node* prev = head_.exchange(node, std::memory_order_acq_rel);
  uintptr_t prev_next = prev->next_.exchange(reinterpret_cast<uintptr_t>(node),
                                             std::memory_order_acq_rel);
  if (1 == actors_.fetch_sub(1, std::memory_order_acq_rel)) {
    DrainMpsc();
  }
  if (prev_next == 0) return Success{};
  DCHECK_NE(prev_next & Node::kWakerPtr, 0u);
  Waker* waker = reinterpret_cast<Waker*>(prev_next & ~Node::kWakerPtr);
  DCHECK_NE(waker, nullptr);
  waker->Wakeup();
  delete waker;
  return Success{};
}

Json Mpsc::PollNextJson() const {
  Json::Object obj;
  obj["what"] = Json::FromString("PollNext");
  Json::Array accepted;
  for (Node* n = accepted_head_; n != nullptr; n = n->spsc_next_) {
    accepted.emplace_back(n->ToJson());
  }
  obj["accepted"] = Json::FromArray(std::move(accepted));
  obj["closed"] = Json::FromBool(tail_ == nullptr);
  obj["queued_tokens"] =
      Json::FromNumber(queued_tokens_.load(std::memory_order_relaxed));
  auto active_tokens = active_tokens_.load(std::memory_order_relaxed);
  obj["active_tokens"] = Json::FromNumber(active_tokens & kActiveTokensMask);
  obj["active_tokens_waker_bit"] =
      Json::FromBool(active_tokens & kActiveTokensWakerBit);
  obj["active_tokens_waking_bit"] =
      Json::FromBool(active_tokens & kActiveTokensWakingBit);
  return Json::FromObject(std::move(obj));
}

Poll<ValueOrFailure<Mpsc::Node*>> Mpsc::PollNext() {
  GRPC_LATENT_SEE_SCOPE("Mpsc::Next");
  Node* accepted_head = accepted_head_;
  if (accepted_head != nullptr) {
    accepted_head_ = accepted_head->spsc_next_;
    return accepted_head;
  }
  if (tail_ == nullptr) return Failure{};
  if (!CheckActiveTokens()) return Pending{};
  auto r = Dequeue();
  if (r.pending()) return Pending{};
  accepted_head = r.value();
  DCHECK_NE(accepted_head, &stub_);
  accepted_head->spsc_next_ = nullptr;
  if (AcceptNode(accepted_head)) {
    Node* accepted_tail = accepted_head;
    while (true) {
      Node* node = DequeueImmediate();
      DCHECK_NE(node, &stub_);
      if (node == nullptr) break;
      node->spsc_next_ = nullptr;
      accepted_tail->spsc_next_ = node;
      accepted_tail = node;
      if (!AcceptNode(node)) break;
    }
  }
  accepted_head_ = accepted_head->spsc_next_;
  return accepted_head;
}

bool Mpsc::AcceptNode(Node* node) {
  GRPC_LATENT_SEE_SCOPE("Mpsc::AcceptNode");
  DCHECK_NE(node, nullptr);
  if (node->state_.fetch_and(255 - Node::kBlockedState,
                             std::memory_order_relaxed) &
      Node::kBlockedState) {
    node->waker_.Wakeup();
  }
  const auto prev_active =
      active_tokens_.fetch_add(node->tokens_, std::memory_order_relaxed);
  return (prev_active & kActiveTokensMask) + node->tokens_ <= max_queued_;
}

bool Mpsc::CheckActiveTokens() {
  GRPC_LATENT_SEE_SCOPE("Mpsc::CheckActiveTokens");
  // First step: see if the active token count is lower than max_queued_.
  // If it's not, we do not supply any nodes.
  auto active_tokens = active_tokens_.load(std::memory_order_relaxed);
  while (true) {
    if ((active_tokens & kActiveTokensMask) > max_queued_) {
      if (active_tokens & (kActiveTokensWakerBit | kActiveTokensWakingBit)) {
        return false;
      }
      active_tokens_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      if (!active_tokens_.compare_exchange_weak(
              active_tokens, active_tokens | kActiveTokensWakerBit,
              std::memory_order_release, std::memory_order_relaxed)) {
        continue;
      }
      return false;
    } else {
      // Observed active tokens less than max_queued_.
      // This must continue for the duration of this code path since it is the
      // only incrementer.
      return true;
    }
  }
}

void Mpsc::DrainMpsc() {
  GRPC_LATENT_SEE_SCOPE("Mpsc::DrainMpsc");
#ifndef NDEBUG
  DCHECK(!drained);
  drained = true;
#endif
  while (true) {
    Node* node = tail_;
    if (node == nullptr) return;
    auto next = node->next_.load(std::memory_order_acquire);
    if (next == 0) {
      tail_ = nullptr;
    } else if (next & Node::kWakerPtr) {
      Waker* waker = reinterpret_cast<Waker*>(next & ~Node::kWakerPtr);
      waker->Wakeup();
      delete waker;
      tail_ = nullptr;
    } else {
      tail_ = reinterpret_cast<Node*>(next);
    }
    if (node != &stub_) ReleaseTokensAndClose(node);
  }
}

Poll<Mpsc::Node*> Mpsc::Dequeue() {
  GRPC_LATENT_SEE_SCOPE("Mpsc::Dequeue");
  Node* tail = tail_;
  uintptr_t next = tail->next_.load(std::memory_order_acquire);
retry_all:
  if (tail == &stub_) {
    if (next == 0) {
      // List is (ephemerally) empty - create a waker so we get woken up
      // when the queue is not empty.
      Waker* waker = new Waker(GetContext<Activity>()->MakeNonOwningWaker());
      if (!tail->next_.compare_exchange_weak(
              next, reinterpret_cast<uintptr_t>(waker) | Node::kWakerPtr,
              std::memory_order_acq_rel)) {
        delete waker;
        goto retry_all;
      }
      return Pending{};  // pending
    }
    if (next & Node::kWakerPtr) {
      // null next waker => list closed
      DCHECK_NE(next, Node::kWakerPtr);
      // List is (ephemerally) empty - but we've already asked to be notified
      // when non-empty.
      return Pending{};  // pending
    }
    tail = reinterpret_cast<Node*>(next);
    tail_ = tail;
    next = tail->next_.load(std::memory_order_acquire);
  }
  if (next != 0 && (next & Node::kWakerPtr) == 0) {
    tail_ = reinterpret_cast<Node*>(next);
    return tail;
  }
  Node* head = head_.load(std::memory_order_acquire);
  DCHECK_NE(head, nullptr);
  if (tail != head) {
    auto tail_next = tail->next_.load(std::memory_order_acquire);
    while (true) {
      if (tail_next != 0 && (tail_next & Node::kWakerPtr) == 0) {
        // Finished adding, retry.
        next = tail->next_.load(std::memory_order_acquire);
        goto retry_all;
      }
      if (tail_next & Node::kWakerPtr) {
        // null next waker => list closed
        DCHECK_NE(tail_next, Node::kWakerPtr);
        // Node still being added, and we've already asked to be notified.
        return Pending{};  // pending
      }
      DCHECK_EQ(tail_next, 0u);
      Waker* waker = new Waker(GetContext<Activity>()->MakeNonOwningWaker());
      // Inform the adder we'd like to be woken up.
      if (!tail->next_.compare_exchange_weak(
              tail_next, reinterpret_cast<uintptr_t>(waker) | Node::kWakerPtr,
              std::memory_order_acq_rel)) {
        delete waker;
        continue;
      }
      return Pending{};  // pending
    }
  }
  // add stub to queue
  PushStub();
  next = tail->next_.load(std::memory_order_acquire);
  if (next != 0 && (next & Node::kWakerPtr) == 0) {
    tail_ = reinterpret_cast<Node*>(next);
    return tail;
  }
  if (next & Node::kWakerPtr) {
    // Node still being added, and we've already asked to be notified.
    return Pending{};  // pending
  }
  DCHECK_EQ(next, 0u);
  Waker* waker = new Waker(GetContext<Activity>()->MakeNonOwningWaker());
  if (!tail->next_.compare_exchange_weak(
          next, reinterpret_cast<uintptr_t>(waker) | Node::kWakerPtr,
          std::memory_order_acq_rel)) {
    delete waker;
    goto retry_all;
  }
  return Pending{};  // pending
}

void Mpsc::PushStub() {
  stub_.next_.store(0, std::memory_order_relaxed);
  Node* prev = head_.exchange(&stub_, std::memory_order_acq_rel);
  DCHECK_NE(prev, nullptr);
  prev->next_.store(reinterpret_cast<uintptr_t>(&stub_),
                    std::memory_order_release);
}

Mpsc::Node* Mpsc::DequeueImmediate() {
  GRPC_LATENT_SEE_SCOPE("Mpsc::DequeueImmediate");
  Node* tail = tail_;
  uintptr_t next = tail->next_.load(std::memory_order_acquire);
  if (tail == &stub_) {
    if (next == 0) {
      // List is (ephemerally) empty.
      return nullptr;  // pending
    }
    if (next & Node::kWakerPtr) {
      // null next waker => list closed
      DCHECK_NE(next, Node::kWakerPtr);
      // List is (ephemerally) empty - but we've already asked to be notified
      // when non-empty.
      return nullptr;  // pending
    }
    tail = reinterpret_cast<Node*>(next);
    tail_ = tail;
    next = tail->next_.load(std::memory_order_acquire);
  }
  if (next != 0 && (next & Node::kWakerPtr) == 0) {
    tail_ = reinterpret_cast<Node*>(next);
    return tail;
  }
  Node* head = head_.load(std::memory_order_acquire);
  if (tail != head) {
    return nullptr;  // pending
  }
  // add stub to queue
  PushStub();
  next = tail->next_.load(std::memory_order_acquire);
  if (next & Node::kWakerPtr) {
    // Node still being added, and we've already asked to be notified.
    return nullptr;  // pending
  }
  if (next == 0) {
    return nullptr;  // pending
  }
  tail_ = reinterpret_cast<Node*>(next);
  return tail;
}

void Mpsc::Close(bool wake_reader) {
  Node* accepted_head = accepted_head_;
  while (accepted_head != nullptr) {
    auto* next = accepted_head->spsc_next_;
    ReleaseActiveTokens(wake_reader, accepted_head->tokens_);
    ReleaseTokensAndClose(accepted_head);
    accepted_head = next;
  }
  accepted_head_ = nullptr;
  if (1 == actors_.fetch_sub(1, std::memory_order_acq_rel)) {
    DrainMpsc();
  }
}

void Mpsc::ReleaseTokens(Node* node) {
  auto prev_queued =
      queued_tokens_.fetch_sub(node->tokens_, std::memory_order_relaxed);
  DCHECK_GE(prev_queued, node->tokens_);
  ReleaseActiveTokens(true, node->tokens_);
  node->Unref();
}

void Mpsc::ReleaseTokensAndClose(Node* node) {
  DCHECK_NE(node, &stub_);
  auto prev_queued =
      queued_tokens_.fetch_sub(node->tokens_, std::memory_order_relaxed);
  DCHECK_GE(prev_queued, node->tokens_);
  // Called when the node has not yet been dequeued -- so we don't need to
  // decrement active tokens_.
  uint8_t state = node->state_.load(std::memory_order_relaxed);
  while (true) {
    DCHECK_EQ(state & Node::kClosedState, 0) << int(state);
    uint8_t new_state = state;
    new_state &= ~Node::kBlockedState;
    new_state |= Node::kClosedState;
    if (!node->state_.compare_exchange_weak(state, new_state,
                                            std::memory_order_acq_rel)) {
      continue;
    }
    break;
  }
  node->waker_.Wakeup();
  node->Unref();
}

void Mpsc::ReleaseActiveTokens(bool wake_reader, uint64_t tokens) {
  DCHECK_EQ(tokens & kActiveTokensMask, tokens);
  auto prev_active =
      active_tokens_.fetch_sub(tokens, std::memory_order_relaxed);
  DCHECK_GE(prev_active & kActiveTokensMask, tokens);
  while ((prev_active & kActiveTokensWakerBit) != 0 &&
         (prev_active & kActiveTokensMask) - tokens <= max_queued_) {
    if (active_tokens_.compare_exchange_weak(
            prev_active,
            (prev_active & kActiveTokensMask) | kActiveTokensWakingBit,
            std::memory_order_acquire, std::memory_order_relaxed)) {
      auto waker = std::move(active_tokens_waker_);
      DCHECK(!waker.is_unwakeable());
      auto prev = active_tokens_.fetch_and(kActiveTokensMask,
                                           std::memory_order_release);
      DCHECK_EQ(prev & (kActiveTokensWakerBit | kActiveTokensWakingBit),
                kActiveTokensWakingBit)
          << prev;
      if (wake_reader) waker.Wakeup();
      return;
    }
  }
}

Json::Object Mpsc::ToJson() const {
  Json::Object obj;
  obj["max_queued"] = Json::FromNumber(max_queued_);
  obj["active_tokens"] =
      Json::FromNumber(active_tokens_.load(std::memory_order_relaxed));
  obj["queued_tokens"] =
      Json::FromNumber(queued_tokens_.load(std::memory_order_relaxed));
  return obj;
}

}  // namespace grpc_core::mpscpipe_detail
