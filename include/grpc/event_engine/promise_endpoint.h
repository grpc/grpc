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

#ifndef GRPC_EVENT_ENGINE_PROMISE_ENDPOINT_H
#define GRPC_EVENT_ENGINE_PROMISE_ENDPOINT_H

#include <memory>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/promise/promise.h"

namespace grpc {

namespace experimental {

class PromiseEndpoint {
 public:
  PromiseEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint,
      grpc_event_engine::experimental::SliceBuffer already_received);
  ~PromiseEndpoint();

  grpc_core::Promise<absl::Status> Write(
      grpc_event_engine::experimental::SliceBuffer data);
  grpc_core::Promise<
      absl::StatusOr<grpc_event_engine::experimental::SliceBuffer>>
  Read(size_t num_bytes);
  grpc_core::Promise<absl::StatusOr<grpc_event_engine::experimental::Slice>>
  ReadSlice(size_t length);
  grpc_core::Promise<absl::StatusOr<uint8_t>> ReadByte();

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  /// data for writes
  /// TODO: handle race condition
  grpc_event_engine::experimental::SliceBuffer write_buffer_;
  absl::optional<absl::Status> write_result_;

  /// data for reads
  /// TODO: handle race condition
  grpc_event_engine::experimental::SliceBuffer read_buffer_;
  grpc_event_engine::experimental::SliceBuffer current_read_buffer_;
  absl::optional<absl::Status> read_result_;
};

}  // namespace experimental

}  // namespace grpc

#endif
