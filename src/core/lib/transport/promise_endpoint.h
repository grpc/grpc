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
#include "src/core/lib/promise/activity.h"
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
  ~PromiseEndpoint();
  /// Prevent copying and moving of PromiseEndpoint.
  PromiseEndpoint(const PromiseEndpoint&) = delete;
  PromiseEndpoint(PromiseEndpoint&&) = delete;

  // Returns a promise that resolves to a `absl::Status` indicating the result
  // of the write operation.
  //
  // Concurrent writes are not supported, which means callers should not call
  // `Write()` before the previous write finishes. Doing that results in
  // undefined behavior.
  auto Write(SliceBuffer data) {
    {
      MutexLock lock(&write_mutex_);
      // Assert previous write finishes.
      GPR_ASSERT(!write_result_.has_value());
      // TODO(ladynana): Replace this with `SliceBufferCast<>` when it is
      // available.
      grpc_slice_buffer_swap(write_buffer_.c_slice_buffer(),
                             data.c_slice_buffer());
    }
    // If `Write()` returns true immediately, the callback will not be called.
    // We still need to call our callback to pick up the result.
    if (endpoint_->Write(std::bind(&PromiseEndpoint::WriteCallback, this,
                                   std::placeholders::_1),
                         &write_buffer_,
                         nullptr /* uses default arguments */)) {
      WriteCallback(absl::OkStatus());
    }
    return [this]() -> Poll<absl::Status> {
      MutexLock lock(&write_mutex_);
      // If current write isn't finished return `Pending()`, else return write
      // result.
      if (!write_result_.has_value()) {
        write_waker_ = Activity::current()->MakeNonOwningWaker();
        return Pending();
      } else {
        const auto ret = *write_result_;
        write_result_.reset();
        return ret;
      }
    };
  }

  // Returns a promise that resolves to `SliceBuffer` with
  // `num_bytes` bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `Read()` before the previous read finishes. Doing that results in
  // undefined behavior.
  auto Read(size_t num_bytes) {
    ReleasableMutexLock lock(&read_mutex_);
    // Assert previous read finishes.
    GPR_ASSERT(!read_result_.has_value());
    // Should not have pending reads.
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);
    if (read_buffer_.Length() < num_bytes) {
      lock.Release();
      // Set read args with hinted bytes.
      grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
          read_args = {static_cast<int64_t>(num_bytes)};
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result and
      // maybe do further reads.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                    std::placeholders::_1, num_bytes),
                          &pending_read_buffer_, &read_args)) {
        ReadCallback(absl::OkStatus(), num_bytes);
      }
    } else {
      read_result_ = absl::OkStatus();
    }
    return [this, num_bytes]() -> Poll<absl::StatusOr<SliceBuffer>> {
      MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        // If current read isn't finished, return `Pending()`.
        read_waker_ = Activity::current()->MakeNonOwningWaker();
        return Pending();
      } else if (!read_result_->ok()) {
        // If read fails, return error.
        const absl::Status ret = *read_result_;
        read_result_.reset();
        return ret;
      } else {
        // If read succeeds, return `SliceBuffer` with `num_bytes` bytes.
        SliceBuffer ret;
        grpc_slice_buffer_move_first(read_buffer_.c_slice_buffer(), num_bytes,
                                     ret.c_slice_buffer());
        read_result_.reset();
        return std::move(ret);
      }
    };
  }

  // Returns a promise that resolves to `Slice` with at least
  // `num_bytes` bytes which should be less than INT64_MAX bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `ReadSlice()` before the previous read finishes. Doing that results in
  // undefined behavior.
  auto ReadSlice(size_t num_bytes) {
    ReleasableMutexLock lock(&read_mutex_);
    // Assert previous read finishes.
    GPR_ASSERT(!read_result_.has_value());
    // Should not have pending reads.
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);
    if (read_buffer_.Length() < num_bytes) {
      lock.Release();
      // Set read args with num_bytes as hint.
      grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
          read_args = {static_cast<int64_t>(num_bytes)};
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result
      // and maybe do further reads.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                    std::placeholders::_1, num_bytes),
                          &pending_read_buffer_, &read_args)) {
        ReadCallback(absl::OkStatus(), num_bytes);
      }
    } else {
      read_result_ = absl::OkStatus();
    }
    return [this, num_bytes]() -> Poll<absl::StatusOr<Slice>> {
      MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        // If current read isn't finished, return `Pending()`.
        read_waker_ = Activity::current()->MakeNonOwningWaker();
        return Pending();
      } else if (!read_result_->ok()) {
        // If read fails, return error.
        const auto ret = *read_result_;
        read_result_.reset();
        return ret;
      }
      // If read succeeds, return `Slice` with `num_bytes`.
      else if (read_buffer_.RefSlice(0).size() == num_bytes) {
        read_result_.reset();
        return Slice(read_buffer_.TakeFirst().TakeCSlice());
      } else {
        // TODO(ladynana): avoid memcpy when read_buffer_.RefSlice(0).size() is
        // different from `num_bytes`.
        MutableSlice ret = MutableSlice::CreateUninitialized(num_bytes);
        read_buffer_.MoveFirstNBytesIntoBuffer(num_bytes, ret.data());
        read_result_.reset();
        return Slice(std::move(ret));
      }
    };
  }

  // Returns a promise that resolves to a byte with type `uint8_t`.
  auto ReadByte() {
    ReleasableMutexLock lock(&read_mutex_);
    // Assert previous read finishes.
    GPR_ASSERT(!read_result_.has_value());
    // Should not have pending reads.
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);
    if (read_buffer_.Length() == 0u) {
      lock.Release();
      // If `Read()` returns true immediately, the callback will not be called.
      // We still need to call our callback to pick up the result and maybe do
      // further reads.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadByteCallback, this,
                                    std::placeholders::_1),
                          &pending_read_buffer_, nullptr)) {
        ReadByteCallback(absl::OkStatus());
      }
    } else {
      read_result_ = absl::OkStatus();
    }
    return [this]() -> Poll<absl::StatusOr<uint8_t>> {
      MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        // If current read isn't finished, return `Pending()`.
        read_waker_ = Activity::current()->MakeNonOwningWaker();
        return Pending();
      } else if (!read_result_->ok()) {
        // If read fails, return error.
        const auto ret = *read_result_;
        read_result_.reset();
        return ret;
      } else {
        // If read succeeds, return a byte with type `uint8_t`.
        uint8_t ret = 0u;
        read_buffer_.MoveFirstNBytesIntoBuffer(1, &ret);
        read_result_.reset();
        return ret;
      }
    };
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  // Data used for writes.
  // TODO(ladynana): Remove this write_mutex_ and use `atomic<bool>
  // write_complete_` as write guard.
  Mutex write_mutex_;
  // Write buffer used for `EventEngine::Endpoint::Write()` to ensure the
  // memory behind the buffer is not lost.
  grpc_event_engine::experimental::SliceBuffer write_buffer_;
  // Used for store the result from `EventEngine::Endpoint::Write()`.
  // `write_result_.has_value() == true` means the value has not been polled
  // yet.
  absl::optional<absl::Status> write_result_ ABSL_GUARDED_BY(write_mutex_);
  Waker write_waker_ ABSL_GUARDED_BY(write_mutex_);

  // Callback function used for `EventEngine::Endpoint::Write()`.
  void WriteCallback(absl::Status status);

  // Data used for reads
  // TODO(ladynana): Remove this read_mutex_ and use `atomic<bool>
  // read_complete_` as read guard.
  Mutex read_mutex_;
  // Read buffer used for storing successful reads given by
  // `EventEngine::Endpoint` but not yet requested by the caller.
  grpc_event_engine::experimental::SliceBuffer read_buffer_;
  // Buffer used to accept data from `EventEngine::Endpoint`.
  // Every time after a successful read from `EventEngine::Endpoint`, the data
  // in this buffer should be appended to `read_buffer_`.
  grpc_event_engine::experimental::SliceBuffer pending_read_buffer_;
  // Used for store the result from `EventEngine::Endpoint::Read()`.
  // `read_result_.has_value() == true` means the value has not been polled
  // yet.
  absl::optional<absl::Status> read_result_ ABSL_GUARDED_BY(read_mutex_);
  Waker read_waker_ ABSL_GUARDED_BY(read_mutex_);

  // Callback function used for `EventEngine::Endpoint::Read()` shared between
  // `Read()` and `ReadSlice()`.
  void ReadCallback(absl::Status status, size_t num_bytes_requested);
  // Callback function used for `EventEngine::Endpoint::Read()` in `ReadByte()`.
  void ReadByteCallback(absl::Status status);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H