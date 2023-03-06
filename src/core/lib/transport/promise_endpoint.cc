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
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc {

namespace internal {

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_core::SliceBuffer already_received)
    : endpoint_(std::move(endpoint)),
      write_buffer_(),
      write_result_(),
      pending_read_buffer_(),
      read_result_() {
  GPR_ASSERT(endpoint_ != nullptr);

  // TODO(ralphchung): Replace this with `SliceBufferCast<>` when it is
  // available. It should be something like the following.
  //
  // read_buffer_ = std::move(
  //     SliceBufferCast<grpc_event_engine::experimental::SliceBuffer&>(
  //         already_received));
  grpc_slice_buffer_swap(read_buffer_.c_slice_buffer(),
                         already_received.c_slice_buffer());
}

PromiseEndpoint::~PromiseEndpoint() {
  // Last write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());
  // Last read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());
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
  grpc_core::MutexLock lock(&write_mutex_);
  write_result_ = status;
  write_waker_.Wakeup();
}

void PromiseEndpoint::ReadCallback(
    absl::Status status, size_t num_bytes_requested,
    absl::optional<
        struct grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs>
        requested_read_args) {
  if (!status.ok()) {
    // invalidates all previous reads
    pending_read_buffer_.Clear();
    read_buffer_.Clear();

    grpc_core::MutexLock lock(&read_mutex_);
    read_result_ = status;
    read_waker_.Wakeup();
  } else {
    // appends `pending_read_buffer_` to `read_buffer_`
    pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        pending_read_buffer_.Length(), read_buffer_);
    GPR_DEBUG_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Length() < num_bytes_requested) {
      // A further read is needed.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                    std::placeholders::_1, num_bytes_requested,
                                    requested_read_args),
                          &pending_read_buffer_,
                          requested_read_args.has_value()
                              ? (&(requested_read_args.value()))
                              : nullptr /* uses default arguments */)) {
        // If read call returns true immediately, the callback will not be
        // called. We still need to call our callback to pick up the result and
        // maybe do further reads.
        ReadCallback(absl::OkStatus(), num_bytes_requested,
                     requested_read_args);
      }
    } else {
      grpc_core::MutexLock lock(&read_mutex_);
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
  grpc_core::MutexLock lock(&read_mutex_);
  read_result_ = status;
  read_waker_.Wakeup();
}

}  // namespace internal

}  // namespace grpc
