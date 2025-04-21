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

#include "src/core/lib/transport/promise_endpoint.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/sync.h"

namespace grpc_core {

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    SliceBuffer already_received)
    : endpoint_(std::move(endpoint)) {
  CHECK_NE(endpoint_, nullptr);
  read_state_->endpoint = endpoint_;
  // TODO(ladynana): Replace this with `SliceBufferCast<>` when it is
  // available.
  grpc_slice_buffer_swap(read_state_->buffer.c_slice_buffer(),
                         already_received.c_slice_buffer());
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetPeerAddress() const {
  return endpoint_->GetPeerAddress();
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetLocalAddress() const {
  return endpoint_->GetLocalAddress();
}

void PromiseEndpoint::ReadState::Complete(absl::Status status,
                                          const size_t num_bytes_requested) {
  while (true) {
    if (!status.ok()) {
      // Invalidates all previous reads.
      pending_buffer.Clear();
      buffer.Clear();
      result = status;
      auto w = std::move(waker);
      complete.store(true, std::memory_order_release);
      w.Wakeup();
      return;
    }
    // Appends `pending_buffer` to `buffer`.
    pending_buffer.MoveFirstNBytesIntoSliceBuffer(pending_buffer.Length(),
                                                  buffer);
    DCHECK(pending_buffer.Count() == 0u);
    if (buffer.Length() < num_bytes_requested) {
      // A further read is needed.
      // Set read args with number of bytes needed as hint.
      grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
          read_args;
      read_args.set_read_hint_bytes(
          static_cast<int64_t>(num_bytes_requested - buffer.Length()));
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result and
      // maybe do further reads.
      auto ep = endpoint.lock();
      if (ep == nullptr) {
        status = absl::UnavailableError("Endpoint closed during read.");
        continue;
      }
      if (ep->Read(
              [self = Ref(), num_bytes_requested](absl::Status status) {
                ExecCtx exec_ctx;
                self->Complete(std::move(status), num_bytes_requested);
              },
              &pending_buffer, std::move(read_args))) {
        continue;
      }
      return;
    }
    result = status;
    auto w = std::move(waker);
    complete.store(true, std::memory_order_release);
    w.Wakeup();
    return;
  }
}

}  // namespace grpc_core
