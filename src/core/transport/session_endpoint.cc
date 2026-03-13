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
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>
#include <utility>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/surface/call.h"
#include "src/core/util/grpc_check.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {

void SessionEndpointReadCallback(void* arg, grpc_error_handle error) {
  auto* tag = static_cast<SessionEndpointTag*>(arg);
  absl::AnyInvocable<void(bool)> callback = std::move(tag->callback);
  if (callback) {
    callback(error.ok());
  }
}

void SessionEndpointWriteCallback(void* arg, grpc_error_handle error) {
  auto* tag = static_cast<SessionEndpointTag*>(arg);
  absl::AnyInvocable<void(bool)> callback = std::move(tag->callback);
  if (callback) {
    callback(error.ok());
  }
}

}  // namespace

grpc_endpoint* SessionEndpoint::Create(grpc_call* call, bool is_client) {
  auto endpoint = std::make_unique<SessionEndpoint>(call, is_client);
  return grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
      std::move(endpoint));
}

SessionEndpoint::State::State(grpc_call* call, bool is_client)
    : call(call), is_client(is_client) {
  const char* ref_reason =
      is_client ? "client_session_endpoint" : "server_session_endpoint";
  Call::FromC(call)->InternalRef(ref_reason);
}

SessionEndpoint::State::~State() {
  const char* ref_reason =
      is_client ? "client_session_endpoint" : "server_session_endpoint";
  Call::FromC(call)->InternalUnref(ref_reason);
}

SessionEndpoint::SessionEndpoint(grpc_call* call, bool is_client)
    : state_(std::make_shared<State>(call, is_client)), is_client_(is_client) {}

SessionEndpoint::~SessionEndpoint() {
  if (!state_->shutdown.exchange(true, std::memory_order_acq_rel)) {
    grpc_call_cancel_internal(state_->call);
  }
}

bool SessionEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           grpc_event_engine::experimental::SliceBuffer* buffer,
                           ReadArgs /*args*/) {
  auto state = state_;
  if (state->shutdown.load(std::memory_order_acquire)) {
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_read)]() mutable {
          cb(absl::UnavailableError("End of stream"));
        });
    return false;
  }
  if (state->read_in_progress.exchange(true, std::memory_order_acquire)) {
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_read)]() mutable {
          cb(absl::InternalError("Read already in progress"));
        });
    return false;
  }

  grpc_op op;
  op.op = GRPC_OP_RECV_MESSAGE;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.recv_message.recv_message = &state->read_buffer;

  state->read_tag.callback = [state, buffer,
                              cb = std::move(on_read)](bool ok) mutable {
    grpc_byte_buffer* read_buffer = state->read_buffer;
    state->read_buffer = nullptr;
    state->read_in_progress.store(false, std::memory_order_release);

    if (!ok || read_buffer == nullptr) {
      if (read_buffer != nullptr) grpc_byte_buffer_destroy(read_buffer);
      cb(absl::UnavailableError("End of stream"));
      return;
    }

    GRPC_CHECK(read_buffer->type == GRPC_BB_RAW);
    grpc_slice_buffer_move_into(&read_buffer->data.raw.slice_buffer,
                                buffer->c_slice_buffer());
    grpc_byte_buffer_destroy(read_buffer);
    cb(absl::OkStatus());
  };

  GRPC_CLOSURE_INIT(&state->read_tag.closure, SessionEndpointReadCallback,
                    &state->read_tag, grpc_schedule_on_exec_ctx);

  grpc_call_error err = grpc_call_start_batch_and_execute(
      state->call, &op, 1, &state->read_tag.closure);
  if (err != GRPC_CALL_OK) {
    state->read_tag.callback = nullptr;
    state->read_in_progress.store(false, std::memory_order_release);
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_read)]() mutable {
          cb(absl::CancelledError("Read failed"));
        });
  }
  return false;
}

bool SessionEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            grpc_event_engine::experimental::SliceBuffer* data,
                            WriteArgs /*args*/) {
  auto state = state_;
  if (state->shutdown.load(std::memory_order_acquire)) {
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_writable)]() mutable {
          cb(absl::UnavailableError("End of stream"));
        });
    return false;
  }
  if (state->write_in_progress.exchange(true, std::memory_order_acquire)) {
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_writable)]() mutable {
          cb(absl::InternalError("Write already in progress"));
        });
    return false;
  }

  grpc_byte_buffer* byte_buffer =
      static_cast<grpc_byte_buffer*>(gpr_malloc(sizeof(grpc_byte_buffer)));
  byte_buffer->type = GRPC_BB_RAW;
  byte_buffer->data.raw.compression = GRPC_COMPRESS_NONE;
  grpc_slice_buffer_init(&byte_buffer->data.raw.slice_buffer);
  grpc_slice_buffer_move_into(data->c_slice_buffer(),
                              &byte_buffer->data.raw.slice_buffer);

  grpc_op op;
  op.op = GRPC_OP_SEND_MESSAGE;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.send_message.send_message = byte_buffer;

  state->write_tag.callback = [state, cb = std::move(on_writable),
                               byte_buffer](bool ok) mutable {
    state->write_in_progress.store(false, std::memory_order_release);
    grpc_byte_buffer_destroy(byte_buffer);
    if (ok) {
      cb(absl::OkStatus());
    } else {
      cb(absl::CancelledError("Write failed"));
    }
  };

  GRPC_CLOSURE_INIT(&state->write_tag.closure, SessionEndpointWriteCallback,
                    &state->write_tag, grpc_schedule_on_exec_ctx);

  grpc_call_error err = grpc_call_start_batch_and_execute(
      state->call, &op, 1, &state->write_tag.closure);
  if (err != GRPC_CALL_OK) {
    state->write_tag.callback = nullptr;
    state->write_in_progress.store(false, std::memory_order_release);
    grpc_byte_buffer_destroy(byte_buffer);
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [cb = std::move(on_writable)]() mutable {
          cb(absl::CancelledError("Write failed"));
        });
  }
  return false;
}

}  // namespace grpc_core
