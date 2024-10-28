// Copyright 2023 gRPC authors.
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

#include "src/core/lib/transport/promise_endpoint.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/sync.h"

namespace grpc_core {

namespace {

const NoDestruct<RefCountedPtr<ArenaFactory>> party_arena_factory{
    SimpleArenaAllocator(0)};

struct WriteState : public RefCounted<WriteState> {
  enum class State : uint8_t {
    kIdle,     // Not writing.
    kWriting,  // Write started, but not completed.
    kWritten,  // Write completed.
  };

  template <typename Sink>
  friend void AbslStringify(Sink& out, State state) {
    switch (state) {
      case State::kIdle:
        out.Append("Idle");
        break;
      case State::kWriting:
        out.Append("Writing");
        break;
      case State::kWritten:
        out.Append("Written");
        break;
    }
  }

  explicit WriteState(
      std::weak_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint)
      : endpoint(endpoint) {}

  std::atomic<State> state{State::kIdle};
  // Write buffer used for `EventEngine::Endpoint::Write()` to ensure the
  // memory behind the buffer is not lost.
  grpc_event_engine::experimental::SliceBuffer buffer;
  // Used to store the result from `EventEngine::Endpoint::Write()`.
  absl::Status result;
  Waker waker;
  // Backing endpoint: we keep this on WriteState as buffer flushes will
  // repeatedly need to interact with the endpoint, possibly after the
  // PromiseEndpoint itself is destroyed.
  std::weak_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint;

  void Complete(absl::Status status) {
    result = std::move(status);
    auto w = std::move(waker);
    auto prev = state.exchange(State::kWritten, std::memory_order_release);
    // Previous state should be Writing. If we got anything else we've entered
    // the callback path twice.
    CHECK(prev == State::kWriting);
    w.Wakeup();
  }

  auto WriteBuffer() {
    // Start write and assert previous write finishes.
    auto prev = state.exchange(State::kWriting, std::memory_order_relaxed);
    CHECK_EQ(prev, State::kIdle);
    absl::optional<absl::Status> status_if_completed;
    if (buffer.Length() == 0) {
      status_if_completed.emplace(absl::OkStatus());
    } else {
      // If `Write()` returns true immediately, the callback will not be called.
      // We still need to call our callback to pick up the result.
      waker = GetContext<Activity>()->MakeNonOwningWaker();
      auto ep = endpoint.lock();
      if (ep == nullptr) {
        status_if_completed.emplace(absl::UnavailableError("Endpoint closed"));
      } else {
        const bool completed = ep->Write(
            [self = Ref()](absl::Status status) {
              ApplicationCallbackExecCtx callback_exec_ctx;
              ExecCtx exec_ctx;
              self->Complete(std::move(status));
            },
            &buffer, nullptr /* uses default arguments */);
        if (completed) {
          waker = Waker();
          status_if_completed.emplace(absl::OkStatus());
        }
      }
    }
    return If(
        status_if_completed.has_value(),
        [this, &status_if_completed]() {
          auto prev = state.exchange(State::kIdle, std::memory_order_relaxed);
          CHECK(prev == State::kWriting);
          return
              [status = std::move(*status_if_completed)]() { return status; };
        },
        GRPC_LATENT_SEE_PROMISE(
            "DelayedWrite", ([this]() {
              return [self = Ref()]() -> Poll<absl::Status> {
                // If current write isn't finished return `Pending()`, else
                // return write result.
                WriteState::State expected = State::kWritten;
                if (self->state.compare_exchange_strong(
                        expected, State::kIdle, std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                  // State was Written, and we changed it to Idle. We can return
                  // the result.
                  return std::move(self->result);
                }
                // State was not Written; since we're polling it must be
                // Writing. Assert that and return Pending.
                CHECK_EQ(expected, State::kWriting);
                return Pending();
              };
            })));
  }
};

}  // namespace

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    SliceBuffer already_received)
    : endpoint_(std::move(endpoint)) {
  auto arena = (*party_arena_factory)->MakeArena();
  write_party_ = Party::Make(arena);
  write_party_->Spawn(
      "flush",
      [write_state = MakeRefCounted<WriteState>(endpoint_),
       buffer_state = buffer_state_]() {
        return Loop(TrySeq(
            [buffer_state, write_state]() -> Poll<absl::Status> {
              ReleasableMutexLock lock(&buffer_state->mu);
              if (buffer_state->queuing.Length() == 0) {
                buffer_state->write_waker =
                    GetContext<Activity>()->MakeNonOwningWaker();
                return Pending{};
              }
              grpc_slice_buffer_swap(write_state->buffer.c_slice_buffer(),
                                     buffer_state->queuing.c_slice_buffer());
              buffer_state->queuing.Clear();
              Waker waker = std::move(buffer_state->buffer_waker);
              lock.Release();
              waker.Wakeup();
              return absl::OkStatus();
            },
            [write_state]() { return write_state->WriteBuffer(); },
            []() -> LoopCtl<absl::Status> { return Continue{}; }));
      },
      [buffer_state = buffer_state_->Ref()](absl::Status status) {
        Waker waker;
        ReleasableMutexLock lock(&buffer_state->mu);
        waker = std::move(buffer_state->buffer_waker);
        buffer_state->failure_status =
            status.ok() ? absl::UnavailableError("Flush party closed") : status;
        lock.Release();
        waker.Wakeup();
      });
  CHECK_NE(endpoint_, nullptr);
  read_state_->endpoint = endpoint_;
  // TODO(ladynana): Replace this with `SliceBufferCast<>` when it is
  // available.
  grpc_slice_buffer_swap(read_state_->buffer.c_slice_buffer(),
                         already_received.c_slice_buffer());
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetPeerAddress() const {
  return endpoint_->GetPeerAddress();
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetLocalAddress() const {
  return endpoint_->GetLocalAddress();
}

void PromiseEndpoint::ReadState::Complete(absl::Status status,
                                          const size_t num_bytes_requested) {
  while (true) {
    if (!status.ok()) {
      // Invalidates all previous reads.
      pending_buffer.Clear();
      buffer.Clear();
      result = status;
      auto w = std::move(waker);
      complete.store(true, std::memory_order_release);
      w.Wakeup();
      return;
    }
    // Appends `pending_buffer` to `buffer`.
    pending_buffer.MoveFirstNBytesIntoSliceBuffer(pending_buffer.Length(),
                                                  buffer);
    DCHECK(pending_buffer.Count() == 0u);
    if (buffer.Length() < num_bytes_requested) {
      // A further read is needed.
      // Set read args with number of bytes needed as hint.
      grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
          read_args = {
              static_cast<int64_t>(num_bytes_requested - buffer.Length())};
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result and
      // maybe do further reads.
      auto ep = endpoint.lock();
      if (ep == nullptr) {
        status = absl::UnavailableError("Endpoint closed during read.");
        continue;
      }
      if (ep->Read(
              [self = Ref(), num_bytes_requested](absl::Status status) {
                ApplicationCallbackExecCtx callback_exec_ctx;
                ExecCtx exec_ctx;
                self->Complete(std::move(status), num_bytes_requested);
              },
              &pending_buffer, &read_args)) {
        continue;
      }
      return;
    }
    result = status;
    auto w = std::move(waker);
    complete.store(true, std::memory_order_release);
    w.Wakeup();
    return;
  }
}

}  // namespace grpc_core
