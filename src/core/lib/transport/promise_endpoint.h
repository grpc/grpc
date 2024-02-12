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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {

// Wrapper around event engine endpoint that provides a promise like API.
class PromiseEndpoint {
 public:
  PromiseEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint,
      SliceBuffer already_received);
  PromiseEndpoint() = default;
  ~PromiseEndpoint() = default;
  /// Prevent copying of PromiseEndpoint; moving is fine.
  PromiseEndpoint(const PromiseEndpoint&) = delete;
  PromiseEndpoint& operator=(const PromiseEndpoint&) = delete;
  PromiseEndpoint(PromiseEndpoint&&) = default;
  PromiseEndpoint& operator=(PromiseEndpoint&&) = default;

  // Returns a promise that resolves to a `absl::Status` indicating the result
  // of the write operation.
  //
  // Concurrent writes are not supported, which means callers should not call
  // `Write()` before the previous write finishes. Doing that results in
  // undefined behavior.
  auto Write(SliceBuffer data) {
    // Assert previous write finishes.
    GPR_ASSERT(!write_state_->complete.load(std::memory_order_relaxed));
    bool completed;
    if (data.Length() == 0) {
      completed = true;
    } else {
      // TODO(ladynana): Replace this with `SliceBufferCast<>` when it is
      // available.
      grpc_slice_buffer_swap(write_state_->buffer.c_slice_buffer(),
                             data.c_slice_buffer());
      // If `Write()` returns true immediately, the callback will not be called.
      // We still need to call our callback to pick up the result.
      write_state_->waker = GetContext<Activity>()->MakeNonOwningWaker();
      completed = endpoint_->Write(
          [write_state = write_state_](absl::Status status) {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            write_state->Complete(std::move(status));
          },
          &write_state_->buffer, nullptr /* uses default arguments */);
      if (completed) write_state_->waker = Waker();
    }
    return If(
        completed, []() { return []() { return absl::OkStatus(); }; },
        [this]() {
          return [write_state = write_state_]() -> Poll<absl::Status> {
            // If current write isn't finished return `Pending()`, else return
            // write result.
            if (!write_state->complete.load(std::memory_order_acquire)) {
              return Pending();
            }
            write_state->complete.store(false, std::memory_order_relaxed);
            return std::move(write_state->result);
          };
        });
  }

  // Returns a promise that resolves to `SliceBuffer` with
  // `num_bytes` bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `Read()` before the previous read finishes. Doing that results in
  // undefined behavior.
  auto Read(size_t num_bytes) {
    // Assert previous read finishes.
    GPR_ASSERT(!read_state_->complete.load(std::memory_order_relaxed));
    // Should not have pending reads.
    GPR_ASSERT(read_state_->pending_buffer.Count() == 0u);
    bool complete = true;
    while (read_state_->buffer.Length() < num_bytes) {
      // Set read args with hinted bytes.
      grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
          read_args = {
              static_cast<int64_t>(num_bytes - read_state_->buffer.Length())};
      // If `Read()` returns true immediately, the callback will not be
      // called.
      read_state_->waker = GetContext<Activity>()->MakeNonOwningWaker();
      if (endpoint_->Read(
              [read_state = read_state_, num_bytes](absl::Status status) {
                ApplicationCallbackExecCtx callback_exec_ctx;
                ExecCtx exec_ctx;
                read_state->Complete(std::move(status), num_bytes);
              },
              &read_state_->pending_buffer, &read_args)) {
        read_state_->waker = Waker();
        read_state_->pending_buffer.MoveFirstNBytesIntoSliceBuffer(
            read_state_->pending_buffer.Length(), read_state_->buffer);
        GPR_DEBUG_ASSERT(read_state_->pending_buffer.Count() == 0u);
      } else {
        complete = false;
        break;
      }
    }
    return If(
        complete,
        [this, num_bytes]() {
          SliceBuffer ret;
          grpc_slice_buffer_move_first(read_state_->buffer.c_slice_buffer(),
                                       num_bytes, ret.c_slice_buffer());
          return [ret = std::move(
                      ret)]() mutable -> Poll<absl::StatusOr<SliceBuffer>> {
            return std::move(ret);
          };
        },
        [this, num_bytes]() {
          return [read_state = read_state_,
                  num_bytes]() -> Poll<absl::StatusOr<SliceBuffer>> {
            if (!read_state->complete.load(std::memory_order_acquire)) {
              return Pending();
            }
            // If read succeeds, return `SliceBuffer` with `num_bytes` bytes.
            if (read_state->result.ok()) {
              SliceBuffer ret;
              grpc_slice_buffer_move_first(read_state->buffer.c_slice_buffer(),
                                           num_bytes, ret.c_slice_buffer());
              read_state->complete.store(false, std::memory_order_relaxed);
              return std::move(ret);
            }
            read_state->complete.store(false, std::memory_order_relaxed);
            return std::move(read_state->result);
          };
        });
  }

  // Returns a promise that resolves to `Slice` with at least
  // `num_bytes` bytes which should be less than INT64_MAX bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `ReadSlice()` before the previous read finishes. Doing that results in
  // undefined behavior.
  auto ReadSlice(size_t num_bytes) {
    return Map(Read(num_bytes),
               [](absl::StatusOr<SliceBuffer> buffer) -> absl::StatusOr<Slice> {
                 if (!buffer.ok()) return buffer.status();
                 return buffer->JoinIntoSlice();
               });
  }

  // Returns a promise that resolves to a byte with type `uint8_t`.
  auto ReadByte() {
    return Map(ReadSlice(1),
               [](absl::StatusOr<Slice> slice) -> absl::StatusOr<uint8_t> {
                 if (!slice.ok()) return slice.status();
                 return (*slice)[0];
               });
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  struct ReadState : public RefCounted<ReadState> {
    std::atomic<bool> complete{false};
    // Read buffer used for storing successful reads given by
    // `EventEngine::Endpoint` but not yet requested by the caller.
    grpc_event_engine::experimental::SliceBuffer buffer;
    // Buffer used to accept data from `EventEngine::Endpoint`.
    // Every time after a successful read from `EventEngine::Endpoint`, the data
    // in this buffer should be appended to `buffer`.
    grpc_event_engine::experimental::SliceBuffer pending_buffer;
    // Used for store the result from `EventEngine::Endpoint::Read()`.
    absl::Status result;
    Waker waker;
    // Backing endpoint: we keep this on ReadState as reads will need to
    // repeatedly read until the target size is hit, and we don't want to access
    // the main object during this dance (indeed the main object may be
    // deleted).
    std::weak_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint;

    void Complete(absl::Status status, size_t num_bytes_requested);
  };

  struct WriteState : public RefCounted<WriteState> {
    std::atomic<bool> complete{false};
    // Write buffer used for `EventEngine::Endpoint::Write()` to ensure the
    // memory behind the buffer is not lost.
    grpc_event_engine::experimental::SliceBuffer buffer;
    // Used for store the result from `EventEngine::Endpoint::Write()`.
    absl::Status result;
    Waker waker;

    void Complete(absl::Status status) {
      result = std::move(status);
      auto w = std::move(waker);
      complete.store(true, std::memory_order_release);
      w.Wakeup();
    }
  };

  RefCountedPtr<WriteState> write_state_ = MakeRefCounted<WriteState>();
  RefCountedPtr<ReadState> read_state_ = MakeRefCounted<ReadState>();
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
