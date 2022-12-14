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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/promise_endpoint.h"

#include <functional>
#include <utility>

#include <grpc/support/log.h>

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

  /// TODO: Is there a better way to convert?
  grpc_slice_buffer_swap(read_buffer_.c_slice_buffer(),
                         already_received.c_slice_buffer());
}

PromiseEndpoint::~PromiseEndpoint() {
  /// Last write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());
  /// Last read result has not been polled.
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

void PromiseEndpoint::ReadCallback(absl::Status status,
                                   size_t num_bytes_requested) {
  if (!status.ok()) {
    pending_read_buffer_.Clear();
    temporary_read_buffer_.Clear();

    grpc_core::MutexLock lock(&read_mutex_);
    read_result_ = status;
    read_waker_.Wakeup();
  } else {
    temporary_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        temporary_read_buffer_.Length(), pending_read_buffer_);

    if (read_buffer_.Length() + pending_read_buffer_.Length() <
        num_bytes_requested) {
      /// A further read is needed.
      endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                std::placeholders::_1, num_bytes_requested),
                      &temporary_read_buffer_,
                      nullptr /* uses default arguments */);
    } else {
      pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
          pending_read_buffer_.Length(), read_buffer_);

      grpc_core::MutexLock lock(&read_mutex_);
      read_result_ = status;
      read_waker_.Wakeup();
    }
  }
}

void PromiseEndpoint::ReadSliceCallback(
    absl::Status status, size_t num_bytes_requested,
    const struct grpc_event_engine::experimental::EventEngine::Endpoint::
        ReadArgs& requested_read_args) {
  if (!status.ok()) {
    pending_read_buffer_.Clear();
    temporary_read_buffer_.Clear();

    grpc_core::MutexLock lock(&read_mutex_);
    read_result_ = status;
    read_waker_.Wakeup();
  } else {
    temporary_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        temporary_read_buffer_.Length(), pending_read_buffer_);

    if (read_buffer_.Length() + pending_read_buffer_.Length() <
        num_bytes_requested) {
      /// A further read is needed.
      endpoint_->Read(std::bind(&PromiseEndpoint::ReadSliceCallback, this,
                                std::placeholders::_1, num_bytes_requested,
                                requested_read_args),
                      &temporary_read_buffer_, &requested_read_args);
    } else {
      pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
          pending_read_buffer_.Length(), read_buffer_);

      grpc_core::MutexLock lock(&read_mutex_);
      read_result_ = status;
      read_waker_.Wakeup();
    }
  }
}

void PromiseEndpoint::ReadByteCallback(absl::Status status) {
  grpc_core::MutexLock lock(&read_mutex_);
  if (!status.ok()) {
    pending_read_buffer_.Clear();
    temporary_read_buffer_.Clear();

    read_result_ = status;
  } else {
    temporary_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        temporary_read_buffer_.Length(), read_buffer_);

    read_result_ = status;
  }
  read_waker_.Wakeup();
}

}  // namespace internal

}  // namespace grpc
