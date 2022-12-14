/*
 * Copyright 2022 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
#define GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <limits>
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

namespace grpc {

namespace internal {

class PromiseEndpoint {
 public:
  PromiseEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint,
      grpc_core::SliceBuffer already_received);
  ~PromiseEndpoint();

  /// returns a promise that resolves to a `absl::Status` indicating the result
  /// of the write operation
  auto Write(grpc_core::SliceBuffer data) {
    {
      grpc_core::MutexLock lock(&write_mutex_);

      /// Previous write result has not been polled.
      GPR_ASSERT(!write_result_.has_value());

      /// TODO: Is there a better way to convert?
      grpc_slice_buffer_swap(data.c_slice_buffer(),
                             write_buffer_.c_slice_buffer());
    }

    endpoint_->Write(
        std::bind(&PromiseEndpoint::WriteCallback, this, std::placeholders::_1),
        &write_buffer_, nullptr /* uses default arguments */);

    return [this]() -> grpc_core::Poll<absl::Status> {
      grpc_core::MutexLock lock(&write_mutex_);
      if (!write_result_.has_value()) {
        write_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else {
        const auto ret = *write_result_;
        write_result_.reset();
        return ret;
      }
    };
  }

  /// returns a promise that resolves to `grpc_core::SliceBuffer` with
  /// `num_bytes` bytes
  auto Read(size_t num_bytes) {
    grpc_core::MutexLock lock(&read_mutex_);

    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    /// Should not have pending reads
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Length() < num_bytes) {
      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                std::placeholders::_1, num_bytes),
                      &temporary_read_buffer_,
                      nullptr /* uses default arguments */);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this, num_bytes]()
               -> grpc_core::Poll<absl::StatusOr<grpc_core::SliceBuffer>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const absl::Status ret = *read_result_;
        read_result_.reset();
        return ret;
      } else {
        grpc_core::SliceBuffer ret;
        grpc_slice_buffer_move_first(read_buffer_.c_slice_buffer(), num_bytes,
                                     ret.c_slice_buffer());

        read_result_.reset();
        return ret;
      }
    };
  }

  /// returns a promise that resolves to `grpc_core::Slice` with at least
  /// `num_bytes` bytes
  auto ReadSlice(size_t num_bytes) {
    grpc_core::MutexLock lock(&read_mutex_);
    if (num_bytes >= static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
      read_result_ = absl::Status(absl::StatusCode::kInvalidArgument,
                                  "Requested size is too big.");
    } else {
      /// Previous read result has not been polled.
      GPR_ASSERT(!read_result_.has_value());

      if (read_buffer_.Length() < num_bytes) {
        const struct grpc_event_engine::experimental::EventEngine::Endpoint::
            ReadArgs read_args = {static_cast<int64_t>(num_bytes)};

        temporary_read_buffer_.Clear();  /// may be redundant
        endpoint_->Read(std::bind(&PromiseEndpoint::ReadSliceCallback, this,
                                  std::placeholders::_1, num_bytes, read_args),
                        &temporary_read_buffer_, &read_args);
      } else {
        read_result_ = absl::OkStatus();
      }
    }

    return [this,
            num_bytes]() -> grpc_core::Poll<absl::StatusOr<grpc_core::Slice>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const auto ret = *read_result_;
        read_result_.reset();
        return ret;
      } else if (read_buffer_.RefSlice(0).size() == num_bytes) {
        return grpc_core::Slice(read_buffer_.TakeFirst().TakeCSlice());
      } else {
        grpc_core::MutableSlice ret =
            grpc_core::MutableSlice::CreateUninitialized(num_bytes);
        read_buffer_.MoveFirstNBytesIntoBuffer(num_bytes, ret.data());

        return grpc_core::Slice(std::move(ret));
      }
    };
  }

  /// returns a promise that resolves to a byte with type `uint8_t`
  auto ReadByte() {
    grpc_core::MutexLock lock(&read_mutex_);

    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    if (read_buffer_.Length() == 0u) {
      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read(std::bind(&PromiseEndpoint::ReadByteCallback, this,
                                std::placeholders::_1),
                      &temporary_read_buffer_, nullptr);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this]() -> grpc_core::Poll<absl::StatusOr<uint8_t>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const auto ret = *read_result_;
        read_result_.reset();

        return ret;
      } else {
        uint8_t ret = 0u;
        read_buffer_.MoveFirstNBytesIntoBuffer(1, &ret);

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

  /// data for writes
  grpc_core::Mutex write_mutex_;
  grpc_event_engine::experimental::SliceBuffer write_buffer_;
  absl::optional<absl::Status> write_result_ ABSL_GUARDED_BY(write_mutex_);
  grpc_core::Waker write_waker_ ABSL_GUARDED_BY(write_mutex_);

  void WriteCallback(absl::Status status);

  /// data for reads
  grpc_core::Mutex read_mutex_;
  grpc_event_engine::experimental::SliceBuffer read_buffer_;
  grpc_event_engine::experimental::SliceBuffer pending_read_buffer_;
  grpc_event_engine::experimental::SliceBuffer temporary_read_buffer_;
  absl::optional<absl::Status> read_result_ ABSL_GUARDED_BY(read_mutex_);
  grpc_core::Waker read_waker_ ABSL_GUARDED_BY(read_mutex_);

  void ReadCallback(absl::Status status, size_t num_bytes_requested);
  void ReadSliceCallback(
      absl::Status status, size_t num_bytes_requested,
      const struct grpc_event_engine::experimental::EventEngine::Endpoint::
          ReadArgs& requested_read_args);
  void ReadByteCallback(absl::Status status);
};

}  // namespace internal

}  // namespace grpc

#endif  // GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
