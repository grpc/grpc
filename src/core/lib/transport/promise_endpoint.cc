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
      read_buffer_(std::move(already_received)),
      pending_read_buffer_(),
      read_result_() {
  GPR_ASSERT(endpoint != nullptr);
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

}  // namespace internal

}  // namespace grpc
