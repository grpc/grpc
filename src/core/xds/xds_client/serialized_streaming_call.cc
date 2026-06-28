//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/xds_client/serialized_streaming_call.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Private nested event handler to intercept callbacks from the underlying
// stream
class SerializedStreamingCall::InternalEventHandler
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit InternalEventHandler(RefCountedPtr<SerializedStreamingCall> parent)
      : parent_(std::move(parent)) {}

  ~InternalEventHandler() override {
    parent_->OnUnderlyingCallDestroyed();
  }

  void OnRequestSent(bool ok) override {
    RefCountedPtr<SerializedStreamingCall> parent = parent_;
    parent->OnRequestSent(ok);
  }

  void OnRecvMessage(absl::string_view payload) override {
    RefCountedPtr<SerializedStreamingCall> parent = parent_;
    parent->OnRecvMessage(payload);
  }

  void OnStatusReceived(absl::Status status) override {
    RefCountedPtr<SerializedStreamingCall> parent = parent_;
    parent->OnStatusReceived(std::move(status));
  }

 private:
  RefCountedPtr<SerializedStreamingCall> parent_;
};

SerializedStreamingCall::SerializedStreamingCall(
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport,
    const char* method,
    std::unique_ptr<
        XdsTransportFactory::XdsTransport::StreamingCall::EventHandler>
        user_event_handler)
    : user_event_handler_(std::move(user_event_handler)),
      receiver_(100),
      mpsc_sender_(receiver_.MakeSender()) {
  RefCountedPtr<Arena> arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  party_ = Party::Make(std::move(arena));
  auto internal_event_handler = std::make_unique<InternalEventHandler>(
      RefAsSubclass<SerializedStreamingCall>());
  underlying_call_ =
      transport->CreateStreamingCall(method, std::move(internal_event_handler));
  party_->Spawn(
      "write_loop",
      [this]() {
        return Loop([this]() {
          return Seq(receiver_.Next(), [this](auto state_or_failure) {
            bool ok = state_or_failure.ok();
            std::shared_ptr<WriteState> state;
            bool has_call = false;
            if (ok) {
              auto queued = std::move(*state_or_failure);
              state = *queued;
              {
                MutexLock lock(&mu_);
                active_write_ = state;
                if (underlying_call_ != nullptr) {
                  underlying_call_->SendMessage(state->payload);
                  has_call = true;
                }
              }
            }
            return Seq(
                [this, ok, has_call]() -> Poll<bool> {
                  if (!ok || !has_call) return false;
                  MutexLock lock(&mu_);
                  // Double-check pattern to prevent a "lost wakeup" race
                  // condition: If OnRequestSent completes after the first check
                  // but before we store the waker, the wakeup is lost. The
                  // second check catches this and returns Ready immediately.
                  if (write_completed_) {
                    write_completed_ = false;
                    return write_ok_;
                  }
                  write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
                  if (write_completed_) {
                    write_completed_ = false;
                    return write_ok_;
                  }
                  return Pending{};
                },
                [this, ok, state, has_call](bool write_ok) -> LoopCtl<absl::Status> {
                  if (!ok) {
                    return absl::CancelledError("Receiver closed");
                  }
                  Waker waker_to_wakeup;
                  if (!write_ok || !has_call) {
                    {
                      MutexLock lock(&state->mu);
                      if (!state->done) {
                        state->status = absl::InternalError(
                            has_call ? "Write failed" : "Stream closed");
                        state->done = true;
                        waker_to_wakeup = std::move(state->waker);
                      }
                    }
                    if (!waker_to_wakeup.is_unwakeable()) {
                      waker_to_wakeup.Wakeup();
                    }
                    if (has_call) {
                      // Fail all pending writes in the queue as well
                      DrainQueueAndFail(absl::InternalError(
                          "Write failed due to previous stream error"));
                    }
                    {
                      MutexLock lock(&mu_);
                      active_write_ = nullptr;
                    }
                    return absl::InternalError(
                        has_call ? "Write failed" : "Stream closed");
                  }
                  {
                    MutexLock lock(&state->mu);
                    if (!state->done) {
                      state->status = absl::OkStatus();
                      state->done = true;
                      waker_to_wakeup = std::move(state->waker);
                    }
                  }
                  if (!waker_to_wakeup.is_unwakeable()) {
                    waker_to_wakeup.Wakeup();
                  }
                  {
                    MutexLock lock(&mu_);
                    active_write_ = nullptr;
                  }
                  // Lazily clean up expired nodes from the cleanup list
                  CleanupExpiredNodes();
                  return Continue{};
                });
          });
        });
      },
      [](absl::Status) {});
}

SerializedStreamingCall::~SerializedStreamingCall() {
  // Free any remaining nodes in the cleanup list to avoid memory leaks
  CleanupNode* head =
      cleanup_list_.exchange(nullptr, std::memory_order_acquire);
  while (head != nullptr) {
    CleanupNode* next = head->next;
    delete head;
    head = next;
  }
}

void SerializedStreamingCall::SendMessage(std::string payload) {
  auto state = std::make_shared<WriteState>();
  state->payload = std::move(payload);
  mpsc_sender_.UnbufferedImmediateSend(std::shared_ptr<WriteState>(state), 1);
}

void SerializedStreamingCall::StartRecvMessage() {
  underlying_call_->StartRecvMessage();
}

void SerializedStreamingCall::DrainQueueAndFail(absl::Status status) {
  // Atomically pop the entire cleanup list
  CleanupNode* head =
      cleanup_list_.exchange(nullptr, std::memory_order_acquire);
  // Iterate and fail all pending
  CleanupNode* curr = head;
  while (curr != nullptr) {
    CleanupNode* next = curr->next;
    if (auto state = curr->state.lock()) {
      Waker waker_to_wakeup;
      {
        MutexLock lock(&state->mu);
        if (!state->done) {
          state->status = status;
          state->done = true;
          waker_to_wakeup = std::move(state->waker);
        }
      }
      if (!waker_to_wakeup.is_unwakeable()) {
        waker_to_wakeup.Wakeup();
      }
    }
    delete curr;  // Free the node
    curr = next;
  }
}

void SerializedStreamingCall::CleanupExpiredNodes() {
  // 1. Atomically swap the list to get exclusive ownership
  CleanupNode* head =
      cleanup_list_.exchange(nullptr, std::memory_order_acquire);
  if (head == nullptr) return;
  // 2. Traverse and keep only alive nodes
  CleanupNode* alive_head = nullptr;
  CleanupNode* curr = head;
  while (curr != nullptr) {
    CleanupNode* next = curr->next;
    if (curr->state.expired()) {
      delete curr;  // Expired! Free the node
    } else {
      // Still alive! Prepend to local alive list
      curr->next = alive_head;
      alive_head = curr;
    }
    curr = next;
  }
  // 3. Prepend the alive list back to the global cleanup list
  if (alive_head != nullptr) {
    // Find the tail of the alive list
    CleanupNode* alive_tail = alive_head;
    while (alive_tail->next != nullptr) {
      alive_tail = alive_tail->next;
    }
    // Atomically prepend back
    CleanupNode* old_head = cleanup_list_.load(std::memory_order_relaxed);
    do {
      alive_tail->next = old_head;
    } while (!cleanup_list_.compare_exchange_weak(old_head, alive_head,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
  }
}

void SerializedStreamingCall::Orphan() {
  // Drain and fail all pending writes using the cleanup list
  DrainQueueAndFail(absl::CancelledError("Stream orphaned"));
  receiver_.MarkClosed();
  // Fail the currently active in-flight write
  std::shared_ptr<WriteState> active_write;
  {
    MutexLock lock(&mu_);
    active_write = std::move(active_write_);
  }
  if (active_write != nullptr) {
    Waker waker_to_wakeup;
    {
      MutexLock lock(&active_write->mu);
      active_write->status = absl::CancelledError("Stream orphaned");
      active_write->done = true;
      waker_to_wakeup = std::move(active_write->waker);
    }
    if (!waker_to_wakeup.is_unwakeable()) {
      waker_to_wakeup.Wakeup();
    }
  }
  // Cancel the party and destroy the underlying call
  party_.reset();
  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_to_destroy;
  {
    MutexLock lock(&mu_);
    call_to_destroy = std::move(underlying_call_);
  }
  call_to_destroy.reset();
  Unref();
}

void SerializedStreamingCall::OnUnderlyingCallDestroyed() {
  MutexLock lock(&mu_);
  underlying_call_.release();
}

void SerializedStreamingCall::OnRequestSent(bool ok) {
  Waker waker_to_wakeup;
  {
    MutexLock lock(&mu_);
    write_completed_ = true;
    write_ok_ = ok;
    waker_to_wakeup = std::move(write_waker_);
  }
  if (!waker_to_wakeup.is_unwakeable()) {
    waker_to_wakeup.Wakeup();
  }
  user_event_handler_->OnRequestSent(ok);
}

void SerializedStreamingCall::OnRecvMessage(absl::string_view payload) {
  user_event_handler_->OnRecvMessage(payload);
}

void SerializedStreamingCall::OnStatusReceived(absl::Status status) {
  DrainQueueAndFail(status);
  Waker waker_to_wakeup;
  {
    MutexLock lock(&mu_);
    if (!write_completed_) {
      write_completed_ = true;
      write_ok_ = false;
      waker_to_wakeup = std::move(write_waker_);
    }
  }
  if (!waker_to_wakeup.is_unwakeable()) {
    waker_to_wakeup.Wakeup();
  }
  user_event_handler_->OnStatusReceived(std::move(status));
}

}  // namespace grpc_core
