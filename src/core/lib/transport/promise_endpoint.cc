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

#include <atomic>
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

namespace grpc_core {

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    SliceBuffer already_received)
    : endpoint_(std::move(endpoint)) {
  GPR_ASSERT(endpoint_ != nullptr);
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
                                          size_t num_bytes_requested) {
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
  GPR_DEBUG_ASSERT(pending_buffer.Count() == 0u);
  if (buffer.Length() < num_bytes_requested) {
    // A further read is needed.
    // Set read args with number of bytes needed as hint.
    grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs read_args =
        {static_cast<int64_t>(num_bytes_requested - buffer.Length())};
    // If `Read()` returns true immediately, the callback will not be
    // called. We still need to call our callback to pick up the result and
    // maybe do further reads.
    auto ep = endpoint.lock();
    if (ep == nullptr) {
      Complete(absl::UnavailableError("Endpoint closed during read."),
               num_bytes_requested);
      return;
    }
    if (ep->Read(
            [self = Ref(), num_bytes_requested](absl::Status status) {
              ApplicationCallbackExecCtx callback_exec_ctx;
              ExecCtx exec_ctx;
              self->Complete(std::move(status), num_bytes_requested);
            },
            &pending_buffer, &read_args)) {
      Complete(std::move(status), num_bytes_requested);
    }
    return;
  }
  result = status;
  auto w = std::move(waker);
  complete.store(true, std::memory_order_release);
  w.Wakeup();
}

}  // namespace grpc_core
