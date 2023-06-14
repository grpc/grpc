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

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {

class PromiseEndpoint {
 public:
  PromiseEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint,
      SliceBuffer already_received);
  ~PromiseEndpoint();

  // Returns a promise that resolves to a `absl::Status` indicating the result
  // of the write operation.
  //
  // Concurrent writes are not supported, which means callers should not call
  // `Write()` before the previous write finishes. Doing that results in
  // undefined behavior.
  ArenaPromise<absl::Status> Write(SliceBuffer data);

  // Returns a promise that resolves to `SliceBuffer` with
  // `num_bytes` bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `Read()` before the previous read finishes. Doing that results in
  // undefined behavior.
  ArenaPromise<absl::StatusOr<SliceBuffer>> Read(size_t num_bytes);

  // Returns a promise that resolves to `Slice` with at least
  // `num_bytes` bytes which should be less than INT64_MAX bytes.
  //
  // Concurrent reads are not supported, which means callers should not call
  // `ReadSlice()` before the previous read finishes. Doing that results in
  // undefined behavior.
  ArenaPromise<absl::StatusOr<Slice>> ReadSlice(size_t num_bytes);

  // Returns a promise that resolves to a byte with type `uint8_t`.
  ArenaPromise<absl::StatusOr<uint8_t>> ReadByte();

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  // Data used for writes.
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
  void ReadCallback(absl::Status status, size_t num_bytes_requested,
                    absl::optional<struct grpc_event_engine::experimental::
                                       EventEngine::Endpoint::ReadArgs>
                        requested_read_arg = absl::nullopt);
  // Callback function used for `EventEngine::Endpoint::Read()` in `ReadByte()`.
  void ReadByteCallback(absl::Status status);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H