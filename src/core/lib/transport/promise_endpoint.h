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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "src/core/lib/event_engine/extensions/chaotic_good_extension.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/sync.h"

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

  // Returns a promise that resolves to a `absl::Status`.
  // An Ok status indicates that the write is queued and may succeed,
  // whilst a failure status indicates that the write was not queued
  // and will never succeed.
  // There is no way to determine whether a specific write succeeds or not.
  //
  // Concurrent writes are not supported, which means callers should not call
  // `Write()` before the previous write finishes. Doing that results in
  // undefined behavior.
  auto Write(SliceBuffer data) {
    return [state = buffer_state_->Ref(),
            data = std::move(data)]() -> Poll<absl::Status> {
      Waker write_waker;
      ReleasableMutexLock lock(&state->mu);
      if (!state->failure_status.ok()) {
        return state->failure_status;
      }
      if (state->queuing.Length() > 0 &&
          state->queuing.Length() + data.Length() > state->queue_limit) {
        state->buffer_waker = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
      if (state->queuing.Length() == 0) {
        write_waker = std::move(state->write_waker);
      }
      state->queuing.Append(std::move(data));
      lock.Release();
      write_waker.Wakeup();
      return absl::OkStatus();
    };
  }

  // Returns a promise that resolves to `SliceBuffer` with
  // `num_bytes` bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `Read()` before the previous read finishes. Doing that results in
  // undefined behavior.
  auto Read(size_t num_bytes) {
    // Assert previous read finishes.
    CHECK(!read_state_->complete.load(std::memory_order_relaxed));
    // Should not have pending reads.
    CHECK_EQ(read_state_->pending_buffer.Count(), 0u);
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
        DCHECK_EQ(read_state_->pending_buffer.Count(), 0u);
      } else {
        complete = false;
        break;
      }
    }
    return If(
        complete,
        [this, num_bytes]() {
          SliceBuffer ret;
          grpc_slice_buffer_move_first_no_inline(
              read_state_->buffer.c_slice_buffer(), num_bytes,
              ret.c_slice_buffer());
          return [ret = std::move(
                      ret)]() mutable -> Poll<absl::StatusOr<SliceBuffer>> {
            return std::move(ret);
          };
        },
        GRPC_LATENT_SEE_PROMISE(
            "DelayedRead", ([this, num_bytes]() {
              return [read_state = read_state_,
                      num_bytes]() -> Poll<absl::StatusOr<SliceBuffer>> {
                if (!read_state->complete.load(std::memory_order_acquire)) {
                  return Pending();
                }
                // If read succeeds, return `SliceBuffer` with `num_bytes`
                // bytes.
                if (read_state->result.ok()) {
                  SliceBuffer ret;
                  grpc_slice_buffer_move_first_no_inline(
                      read_state->buffer.c_slice_buffer(), num_bytes,
                      ret.c_slice_buffer());
                  read_state->complete.store(false, std::memory_order_relaxed);
                  return std::move(ret);
                }
                read_state->complete.store(false, std::memory_order_relaxed);
                return std::move(read_state->result);
              };
            })));
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

  // Enables RPC receive coalescing and alignment of memory holding received
  // RPCs.
  void EnforceRxMemoryAlignmentAndCoalescing() {
    auto* chaotic_good_ext = grpc_event_engine::experimental::QueryExtension<
        grpc_event_engine::experimental::ChaoticGoodExtension>(endpoint_.get());
    if (chaotic_good_ext != nullptr) {
      chaotic_good_ext->EnforceRxMemoryAlignment();
      chaotic_good_ext->EnableRpcReceiveCoalescing();
      if (read_state_->buffer.Length() == 0) {
        return;
      }

      // Copy everything from read_state_->buffer into a single slice and
      // replace the contents of read_state_->buffer with that slice.
      grpc_slice slice = grpc_slice_malloc_large(read_state_->buffer.Length());
      CHECK(reinterpret_cast<uintptr_t>(GRPC_SLICE_START_PTR(slice)) % 64 == 0);
      size_t ofs = 0;
      for (size_t i = 0; i < read_state_->buffer.Count(); i++) {
        memcpy(
            GRPC_SLICE_START_PTR(slice) + ofs,
            GRPC_SLICE_START_PTR(
                read_state_->buffer.c_slice_buffer()->slices[i]),
            GRPC_SLICE_LENGTH(read_state_->buffer.c_slice_buffer()->slices[i]));
        ofs +=
            GRPC_SLICE_LENGTH(read_state_->buffer.c_slice_buffer()->slices[i]);
      }

      read_state_->buffer.Clear();
      read_state_->buffer.AppendIndexed(
          grpc_event_engine::experimental::Slice(slice));
      DCHECK(read_state_->buffer.Length() == ofs);
    }
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

  struct BufferState : public RefCounted<BufferState> {
    Mutex mu;
    absl::Status failure_status ABSL_GUARDED_BY(mu);
    size_t queue_limit ABSL_GUARDED_BY(mu);
    Waker buffer_waker ABSL_GUARDED_BY(mu);
    Waker write_waker ABSL_GUARDED_BY(mu);
    SliceBuffer queuing ABSL_GUARDED_BY(mu);
  };

  RefCountedPtr<BufferState> buffer_state_ = MakeRefCounted<BufferState>();
  RefCountedPtr<ReadState> read_state_ = MakeRefCounted<ReadState>();
  RefCountedPtr<Party> write_party_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
