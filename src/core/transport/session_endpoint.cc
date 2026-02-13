//
//
// Copyright 2026 gRPC authors.
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
//
//

#include "src/core/transport/session_endpoint.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/call.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {
struct SessionEndpointImpl {
  grpc_endpoint base;
  std::shared_ptr<SessionEndpoint::State> state;
  bool is_client;
};

void SessionEndpointReadCallback(void* arg, grpc_error_handle error) {
  auto* state = static_cast<SessionEndpoint::State*>(arg);
  absl::AnyInvocable<void(bool)> callback = std::move(state->read_tag.callback);
  if (callback) {
    callback(error.ok());
  }
}

void SessionEndpointWriteCallback(void* arg, grpc_error_handle error) {
  auto* state = static_cast<SessionEndpoint::State*>(arg);
  absl::AnyInvocable<void(bool)> callback =
      std::move(state->write_tag.callback);
  if (callback) {
    callback(error.ok());
  }
}

void EndpointRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                  grpc_closure* cb, bool /*urgent*/,
                  int /*min_progress_size*/) {
  auto* self = reinterpret_cast<SessionEndpointImpl*>(ep);
  grpc_call* call = self->state->call.load(std::memory_order_acquire);
  if (call == nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, cb, absl::UnavailableError("End of stream"));
    return;
  }
  if (self->state->read_in_progress.exchange(true, std::memory_order_acquire)) {
    ExecCtx::Run(DEBUG_LOCATION, cb,
                 absl::InternalError("Read already in progress"));
    return;
  }

  grpc_op op;
  op.op = GRPC_OP_RECV_MESSAGE;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.recv_message.recv_message = &self->state->read_buffer;

  self->state->read_tag.callback = [state = self->state, slices,
                                    cb](bool ok) mutable {
    grpc_byte_buffer* read_buffer = state->read_buffer;
    state->read_buffer = nullptr;
    state->read_in_progress.store(false, std::memory_order_release);

    if (!ok || read_buffer == nullptr) {
      if (read_buffer != nullptr) grpc_byte_buffer_destroy(read_buffer);
      ExecCtx::Run(DEBUG_LOCATION, cb, absl::UnavailableError("End of stream"));
      return;
    }

    grpc_byte_buffer_reader reader;
    if (grpc_byte_buffer_reader_init(&reader, read_buffer)) {
      grpc_slice slice;
      while (grpc_byte_buffer_reader_next(&reader, &slice)) {
        grpc_slice_buffer_add(slices, slice);
      }
      grpc_byte_buffer_reader_destroy(&reader);
    }
    grpc_byte_buffer_destroy(read_buffer);
    ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
  };

  GRPC_CLOSURE_INIT(&self->state->read_tag.closure, SessionEndpointReadCallback,
                    self->state.get(), grpc_schedule_on_exec_ctx);

  grpc_call_error err = grpc_call_start_batch_and_execute(
      call, &op, 1, &self->state->read_tag.closure);
  if (err != GRPC_CALL_OK) {
    self->state->read_tag.callback = nullptr;
    self->state->read_in_progress.store(false, std::memory_order_release);
    ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError("Read failed"));
  }
}

void EndpointWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb,
                   grpc_event_engine::experimental::EventEngine::Endpoint::
                       WriteArgs /*args*/) {
  auto* self = reinterpret_cast<SessionEndpointImpl*>(ep);
  grpc_call* call = self->state->call.load(std::memory_order_acquire);
  if (call == nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, cb, absl::UnavailableError("End of stream"));
    return;
  }
  if (self->state->write_in_progress.exchange(true,
                                              std::memory_order_acquire)) {
    ExecCtx::Run(DEBUG_LOCATION, cb,
                 absl::InternalError("Write already in progress"));
    return;
  }

  grpc_byte_buffer* byte_buffer =
      grpc_raw_byte_buffer_create(slices->slices, slices->count);

  grpc_op op;
  op.op = GRPC_OP_SEND_MESSAGE;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.send_message.send_message = byte_buffer;

  self->state->write_tag.callback = [state = self->state, cb,
                                     byte_buffer](bool ok) mutable {
    state->write_in_progress.store(false, std::memory_order_release);
    grpc_byte_buffer_destroy(byte_buffer);
    if (ok) {
      ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
    } else {
      ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError("Write failed"));
    }
  };

  GRPC_CLOSURE_INIT(&self->state->write_tag.closure,
                    SessionEndpointWriteCallback, self->state.get(),
                    grpc_schedule_on_exec_ctx);

  grpc_call_error err = grpc_call_start_batch_and_execute(
      call, &op, 1, &self->state->write_tag.closure);
  if (err != GRPC_CALL_OK) {
    self->state->write_tag.callback = nullptr;
    self->state->write_in_progress.store(false, std::memory_order_release);
    grpc_byte_buffer_destroy(byte_buffer);
    ExecCtx::Run(DEBUG_LOCATION, cb, absl::CancelledError("Write failed"));
  }
}

void EndpointAddToPollset(grpc_endpoint* /*ep*/, grpc_pollset* /*pollset*/) {}
void EndpointAddToPollsetSet(grpc_endpoint* /*ep*/,
                             grpc_pollset_set* /*pollset*/) {}
void EndpointDeleteFromPollsetSet(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}
void EndpointDestroy(grpc_endpoint* ep) {
  auto* self = reinterpret_cast<SessionEndpointImpl*>(ep);
  grpc_call* call =
      self->state->call.exchange(nullptr, std::memory_order_acq_rel);
  if (call != nullptr) {
    grpc_call_cancel_internal(call);
    const char* ref_reason =
        self->is_client ? "client_session_endpoint" : "server_session_endpoint";
    Call::FromC(call)->InternalUnref(ref_reason);
  }
  delete self;
}

absl::string_view EndpointGetPeerAddress(grpc_endpoint* /*ep*/) { return ""; }
absl::string_view EndpointGetLocalAddress(grpc_endpoint* /*ep*/) { return ""; }
int EndpointGetFd(grpc_endpoint* /*ep*/) { return -1; }
bool EndpointCanTrackErr(grpc_endpoint* /*ep*/) { return false; }

grpc_endpoint_vtable vtable = {EndpointRead,
                               EndpointWrite,
                               EndpointAddToPollset,
                               EndpointAddToPollsetSet,
                               EndpointDeleteFromPollsetSet,
                               EndpointDestroy,
                               EndpointGetPeerAddress,
                               EndpointGetLocalAddress,
                               EndpointGetFd,
                               EndpointCanTrackErr};

}  // namespace

grpc_endpoint* SessionEndpoint::Create(grpc_call* call, bool is_client) {
  auto* endpoint = new SessionEndpointImpl();
  endpoint->base.vtable = &vtable;
  endpoint->state = std::make_shared<State>(call);
  endpoint->is_client = is_client;
  const char* ref_reason =
      is_client ? "client_session_endpoint" : "server_session_endpoint";
  Call::FromC(call)->InternalRef(ref_reason);
  return &endpoint->base;
}

}  // namespace grpc_core
