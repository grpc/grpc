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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/promise_endpoint.h"

#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    SliceBuffer already_received)
    : endpoint_(std::move(endpoint)) {
  GPR_ASSERT(endpoint_ != nullptr);

  // TODO(ladynana): Replace this with `SliceBufferCast<>` when it is
  // available.
  grpc_slice_buffer_swap(read_buffer_.c_slice_buffer(),
                         already_received.c_slice_buffer());
}

PromiseEndpoint::~PromiseEndpoint() {
  // Last write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());
  // Last read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());
}

// Returns a promise that resolves to a `absl::Status` indicating the result
// of the write operation.
//
// Concurrent writes are not supported, which means callers should not call
// `Write()` before the previous write finishes. Doing that results in
// undefined behavior.
ArenaPromise<absl::Status> PromiseEndpoint::Write(SliceBuffer data) {
  {
    MutexLock lock(&write_mutex_);

    // Previous write result has not been polled.
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
                       &write_buffer_, nullptr /* uses default arguments */)) {
    WriteCallback(absl::OkStatus());
  }

  return [this]() -> Poll<absl::Status> {
    MutexLock lock(&write_mutex_);
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
ArenaPromise<absl::StatusOr<SliceBuffer>> PromiseEndpoint::Read(
    size_t num_bytes) {
  ReleasableMutexLock lock(&read_mutex_);

  // Previous read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());

  // Should not have pending reads.
  GPR_ASSERT(pending_read_buffer_.Count() == 0u);

  if (read_buffer_.Length() < num_bytes) {
    lock.Release();
    // If `Read()` returns true immediately, the callback will not be
    // called. We still need to call our callback to pick up the result and
    // maybe do further reads.
    if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                  std::placeholders::_1, num_bytes,
                                  absl::nullopt /* uses default arguments */),
                        &pending_read_buffer_,
                        nullptr /* uses default arguments */)) {
      ReadCallback(absl::OkStatus(), num_bytes, absl::nullopt);
    }
  } else {
    read_result_ = absl::OkStatus();
  }

  return [this, num_bytes]() -> Poll<absl::StatusOr<SliceBuffer>> {
    MutexLock lock(&read_mutex_);
    if (!read_result_.has_value()) {
      read_waker_ = Activity::current()->MakeNonOwningWaker();
      return Pending();
    } else if (!read_result_->ok()) {
      const absl::Status ret = *read_result_;
      read_result_.reset();
      return ret;
    } else {
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
ArenaPromise<absl::StatusOr<Slice>> PromiseEndpoint::ReadSlice(
    size_t num_bytes) {
  ReleasableMutexLock lock(&read_mutex_);
  if (num_bytes >= static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    read_result_ = absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat(
            "Requested size is bigger than the maximum supported size %lld.",
            std::numeric_limits<int64_t>::max()));
  } else {
    // Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    // Should not have pending reads.
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Length() < num_bytes) {
      lock.Release();
      const struct grpc_event_engine::experimental::EventEngine::Endpoint::
          ReadArgs read_args = {static_cast<int64_t>(num_bytes)};

      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result
      // and maybe do further reads.
      if (endpoint_->Read(
              std::bind(&PromiseEndpoint::ReadCallback, this,
                        std::placeholders::_1, num_bytes, read_args),
              &pending_read_buffer_, &read_args)) {
        ReadCallback(absl::OkStatus(), num_bytes, read_args);
      }
    } else {
      read_result_ = absl::OkStatus();
    }
  }

  return [this, num_bytes]() -> Poll<absl::StatusOr<Slice>> {
    MutexLock lock(&read_mutex_);
    if (!read_result_.has_value()) {
      read_waker_ = Activity::current()->MakeNonOwningWaker();
      return Pending();
    } else if (!read_result_->ok()) {
      const auto ret = *read_result_;
      read_result_.reset();
      return ret;
    } else if (read_buffer_.RefSlice(0).size() == num_bytes) {
      read_result_.reset();
      return Slice(read_buffer_.TakeFirst().TakeCSlice());
    } else {
      MutableSlice ret = MutableSlice::CreateUninitialized(num_bytes);
      read_buffer_.MoveFirstNBytesIntoBuffer(num_bytes, ret.data());

      read_result_.reset();
      return Slice(std::move(ret));
    }
  };
}

// Returns a promise that resolves to a byte with type `uint8_t`.
ArenaPromise<absl::StatusOr<uint8_t>> PromiseEndpoint::ReadByte() {
  ReleasableMutexLock lock(&read_mutex_);

  // Previous read result has not been polled.
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
      read_waker_ = Activity::current()->MakeNonOwningWaker();
      return Pending();
    } else if (!read_result_->ok()) {
      const auto ret = *read_result_;
      read_result_.reset();
      return ret;
    } else {
      uint8_t ret = 0u;
      read_buffer_.MoveFirstNBytesIntoBuffer(1, &ret);

      read_result_.reset();
      return ret;
    }
  };
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetPeerAddress() const {
  return endpoint_->GetPeerAddress();
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetLocalAddress() const {
  return endpoint_->GetLocalAddress();
}

void PromiseEndpoint::WriteCallback(absl::Status status) {
  MutexLock lock(&write_mutex_);
  write_result_ = status;
  write_waker_.Wakeup();
}

void PromiseEndpoint::ReadCallback(
    absl::Status status, size_t num_bytes_requested,
    absl::optional<
        struct grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs>
        requested_read_args) {
  if (!status.ok()) {
    // Invalidates all previous reads.
    pending_read_buffer_.Clear();
    read_buffer_.Clear();

    MutexLock lock(&read_mutex_);
    read_result_ = status;
    read_waker_.Wakeup();
  } else {
    // Appends `pending_read_buffer_` to `read_buffer_`.
    pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        pending_read_buffer_.Length(), read_buffer_);
    GPR_DEBUG_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Length() < num_bytes_requested) {
      // A further read is needed.
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result and
      // maybe do further reads.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                    std::placeholders::_1, num_bytes_requested,
                                    requested_read_args),
                          &pending_read_buffer_,
                          requested_read_args.has_value()
                              ? (&(requested_read_args.value()))
                              : nullptr /* uses default arguments */)) {
        ReadCallback(absl::OkStatus(), num_bytes_requested,
                     requested_read_args);
      }
    } else {
      MutexLock lock(&read_mutex_);
      read_result_ = status;
      read_waker_.Wakeup();
    }
  }
}

void PromiseEndpoint::ReadByteCallback(absl::Status status) {
  if (!status.ok()) {
    // invalidates all previous reads
    pending_read_buffer_.Clear();
    read_buffer_.Clear();
  } else {
    pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        pending_read_buffer_.Length(), read_buffer_);
  }
  MutexLock lock(&read_mutex_);
  read_result_ = status;
  read_waker_.Wakeup();
}

}  // namespace grpc_core
