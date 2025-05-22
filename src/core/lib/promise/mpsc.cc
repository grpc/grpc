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
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::Enqueue");
  const auto tokens = node->tokens_;
  if (tokens > 0 &&
      queued_tokens_.fetch_add(tokens, std::memory_order_relaxed) + tokens >
          max_queued_) {
    node->waker_ = GetContext<Activity>()->MakeOwningWaker();
    node->state_.store(2 | Node::kBlockedState, std::memory_order_release);
  } else {
    node->state_.store(2, std::memory_order_release);
  }
  DCHECK_EQ(node->next_.load(std::memory_order_relaxed), 0);
  Node* prev = head_.load(std::memory_order_acquire);
  while (true) {
    if (prev == nullptr) {
      // Queue closed - node not yet added to queue.
      node->state_.store(1 | Node::kClosedState, std::memory_order_relaxed);
      return;
    }
    if (head_.compare_exchange_weak(prev, node, std::memory_order_acq_rel)) {
      break;
    }
  }
  uintptr_t prev_next = prev->next_.exchange(reinterpret_cast<uintptr_t>(node),
                                             std::memory_order_acq_rel);
  if (prev_next == 0) return;
  DCHECK_NE(prev_next & Node::kWakerPtr, 0);
  Waker* waker = reinterpret_cast<Waker*>(prev_next & ~Node::kWakerPtr);
  if (waker == nullptr) {
    // Queue closed - node not yet added to queue.
    node->state_.store(1 | Node::kClosedState, std::memory_order_relaxed);
    return;
  }
  waker->Wakeup();
  delete waker;
}

StatusFlag Mpsc::UnbufferedImmediateSend(Node* node) {
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::UnbufferedImmediateSend");
  queued_tokens_.fetch_add(node->tokens_, std::memory_order_relaxed);
  node->state_.store(1, std::memory_order_relaxed);
  Node* prev = head_.exchange(node, std::memory_order_acq_rel);
  if (prev == nullptr) {
    // queue closed.
    delete node;
    return Failure{};
  }
  uintptr_t prev_next = prev->next_.exchange(reinterpret_cast<uintptr_t>(node),
                                             std::memory_order_acq_rel);
  if (prev_next == 0) return Success{};
  DCHECK_NE(prev_next & Node::kWakerPtr, 0);
  Waker* waker = reinterpret_cast<Waker*>(prev_next & ~Node::kWakerPtr);
  if (waker == nullptr) {
    // queue closed.
    delete node;
    DrainMpscFrom(true, prev);
    return Failure{};
  }
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
  obj["active_tokens_waker"] = Json::FromBool(active_tokens_waker_ != nullptr);
  return Json::FromObject(std::move(obj));
}

Poll<ValueOrFailure<Mpsc::Node*>> Mpsc::PollNext() {
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::Next");
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
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::AcceptNode");
  DCHECK_NE(node, nullptr);
  if (node->state_.fetch_and(255 - Node::kBlockedState,
                             std::memory_order_relaxed) &
      Node::kBlockedState) {
    node->waker_.Wakeup();
  }
  const auto prev_active =
      active_tokens_.fetch_add(node->tokens_, std::memory_order_relaxed);
  return (prev_active & kActiveTokensMask) + node->tokens_ < max_queued_;
}

bool Mpsc::CheckActiveTokens() {
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::CheckActiveTokens");
  // First step: see if the active token count is lower than max_queued_.
  // If it's not, we do not supply any nodes.
  auto active_tokens = active_tokens_.load(std::memory_order_relaxed);
  while (true) {
    if ((active_tokens & kActiveTokensMask) > max_queued_) {
      if (active_tokens & kActiveTokensWakerBit) {
        return false;
      }
      if (active_tokens_waker_.load(std::memory_order_relaxed) == nullptr) {
        // Safe to directly store since we are the only writer.
        active_tokens_waker_.store(
            new Waker(GetContext<Activity>()->MakeOwningWaker()),
            std::memory_order_release);
      }
      if (active_tokens_.compare_exchange_weak(
              active_tokens, active_tokens | kActiveTokensWakerBit,
              std::memory_order_relaxed)) {
        return false;
      }
    } else {
      // Observed active tokens less than max_queued_.
      // This must continue for the duration of this code path since it is the
      // only incrementer.
      return true;
    }
  }
}

void Mpsc::DrainMpscFrom(bool wakeup, Node* node) {
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::DrainMpscFrom");
  while (node != nullptr) {
    auto next =
        node->next_.exchange(Node::kWakerPtr, std::memory_order_acquire);
    if (next & Node::kWakerPtr) {
      DCHECK_NE(next, Node::kWakerPtr);
      Waker* waker = reinterpret_cast<Waker*>(next & ~Node::kWakerPtr);
      if (wakeup) waker->Wakeup();
      delete waker;
      next = 0;
    }
    if (node != &stub_) ReleaseTokensAndClose(wakeup, node);
    node = reinterpret_cast<Node*>(next);
  }
}

Poll<Mpsc::Node*> Mpsc::Dequeue() {
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::Dequeue");
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
  // Only the reader can close, so we should not see that here.
  DCHECK_NE(head, nullptr);
  if (tail != head) {
    auto tail_next = head->next_.load(std::memory_order_acquire);
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
      DCHECK_EQ(tail_next, 0);
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
  DCHECK_EQ(next, 0);
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
  GRPC_LATENT_SEE_INNER_SCOPE("Mpsc::DequeueImmediate");
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
  // Only the reader can close, so we should not see that here.
  DCHECK_NE(head, nullptr);
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
  auto* head = head_.exchange(nullptr, std::memory_order_relaxed);
  if (head == nullptr) return;
  uintptr_t next = head->next_.exchange(0, std::memory_order_acq_rel);
  if (next & Node::kWakerPtr) {
    Waker* waker = reinterpret_cast<Waker*>(next & ~Node::kWakerPtr);
    DCHECK_NE(waker, nullptr);
    if (wake_reader) waker->Wakeup();
    delete waker;
  } else {
    DCHECK_EQ(next, 0);
  }
  Node* accepted_head = accepted_head_;
  while (accepted_head != nullptr) {
    auto* next = accepted_head->spsc_next_;
    ReleaseActiveTokens(accepted_head->tokens_);
    ReleaseTokensAndClose(wake_reader, accepted_head);
    accepted_head = next;
  }
  accepted_head_ = nullptr;
  DrainMpscFrom(wake_reader, tail_);
  tail_ = nullptr;
}

void Mpsc::ReleaseTokens(Node* node) {
  auto prev_queued =
      queued_tokens_.fetch_sub(node->tokens_, std::memory_order_relaxed);
  DCHECK_GE(prev_queued, node->tokens_);
  ReleaseActiveTokens(node->tokens_);
  node->Unref();
}

void Mpsc::ReleaseTokensAndClose(bool wakeup, Node* node) {
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

void Mpsc::ReleaseActiveTokens(uint64_t tokens) {
  auto prev_active =
      active_tokens_.fetch_sub(tokens, std::memory_order_relaxed);
  DCHECK_GE(prev_active & kActiveTokensMask, tokens);
  if ((prev_active & kActiveTokensWakerBit) != 0 &&
      (prev_active & kActiveTokensMask) - tokens <= max_queued_) {
    if (auto* waker =
            active_tokens_waker_.exchange(nullptr, std::memory_order_acquire);
        waker != nullptr) {
      auto prev = active_tokens_.fetch_and(~kActiveTokensWakerBit,
                                           std::memory_order_relaxed);
      DCHECK_EQ(prev & kActiveTokensMask, kActiveTokensMask) << prev;
      waker->Wakeup();
      delete waker;
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
