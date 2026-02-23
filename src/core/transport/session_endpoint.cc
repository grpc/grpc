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
#include <cstdint>
#include <memory>
#include <utility>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/surface/call.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/latent_see.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {

constexpr int64_t kShutdownBit = static_cast<int64_t>(1) << 32;

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

class SessionEndpointImpl {
 public:
  SessionEndpointImpl(grpc_call* call, bool is_client)
      : call_(call), is_client_(is_client) {
    const char* ref_reason =
        is_client ? "client_session_endpoint" : "server_session_endpoint";
    Call::FromC(call_)->InternalRef(ref_reason);
  }

  ~SessionEndpointImpl() {
    const char* ref_reason =
        is_client_ ? "client_session_endpoint" : "server_session_endpoint";
    Call::FromC(call_)->InternalUnref(ref_reason);
  }

  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer) {
    GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("SessionEndpoint::Read");
    if (!ShutdownRef()) {
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_read)]() mutable {
            cb(absl::UnavailableError("End of stream"));
          });
      return false;
    }

    if (read_in_progress_.exchange(true, std::memory_order_acquire)) {
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_read)]() mutable {
            cb(absl::InternalError("Read already in progress"));
          });
      ShutdownUnref();
      return false;
    }

    grpc_op op;
    op.op = GRPC_OP_RECV_MESSAGE;
    op.flags = 0;
    op.reserved = nullptr;
    op.data.recv_message.recv_message = &read_buffer_;

    Ref();
    read_tag_.callback = [this, buffer,
                          cb = std::move(on_read)](bool ok) mutable {
      GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("SessionEndpoint::Read callback");
      grpc_byte_buffer* read_buffer = read_buffer_;
      read_buffer_ = nullptr;
      read_in_progress_.store(false, std::memory_order_release);

      if (!ok || read_buffer == nullptr) {
        if (read_buffer != nullptr) grpc_byte_buffer_destroy(read_buffer);
        cb(absl::UnavailableError("End of stream"));
        Unref();
        return;
      }

      GRPC_CHECK(read_buffer->type == GRPC_BB_RAW);
      grpc_slice_buffer_move_into(&read_buffer->data.raw.slice_buffer,
                                  buffer->c_slice_buffer());
      grpc_byte_buffer_destroy(read_buffer);
      cb(absl::OkStatus());
      Unref();
    };

    GRPC_CLOSURE_INIT(&read_tag_.closure, SessionEndpointReadCallback,
                      &read_tag_, grpc_schedule_on_exec_ctx);

    grpc_call_error err =
        grpc_call_start_batch_and_execute(call_, &op, 1, &read_tag_.closure);
    if (err != GRPC_CALL_OK) {
      read_tag_.callback = nullptr;
      read_in_progress_.store(false, std::memory_order_release);
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_read)]() mutable {
            cb(absl::CancelledError("Read failed"));
          });
      ShutdownUnref();
      Unref();
      return false;
    }
    ShutdownUnref();
    return false;
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* data) {
    GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("SessionEndpoint::Write");
    if (!ShutdownRef()) {
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_writable)]() mutable {
            cb(absl::UnavailableError("End of stream"));
          });
      return false;
    }

    if (write_in_progress_.exchange(true, std::memory_order_acquire)) {
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_writable)]() mutable {
            cb(absl::InternalError("Write already in progress"));
          });
      ShutdownUnref();
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

    Ref();
    write_tag_.callback = [this, cb = std::move(on_writable),
                           byte_buffer](bool ok) mutable {
      GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("SessionEndpoint::Write callback");
      write_in_progress_.store(false, std::memory_order_release);
      grpc_byte_buffer_destroy(byte_buffer);
      if (ok) {
        cb(absl::OkStatus());
      } else {
        cb(absl::CancelledError("Write failed"));
      }
      Unref();
    };

    GRPC_CLOSURE_INIT(&write_tag_.closure, SessionEndpointWriteCallback,
                      &write_tag_, grpc_schedule_on_exec_ctx);

    grpc_call_error err =
        grpc_call_start_batch_and_execute(call_, &op, 1, &write_tag_.closure);
    if (err != GRPC_CALL_OK) {
      write_tag_.callback = nullptr;
      write_in_progress_.store(false, std::memory_order_release);
      grpc_byte_buffer_destroy(byte_buffer);
      grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          [cb = std::move(on_writable)]() mutable {
            cb(absl::CancelledError("Write failed"));
          });
      ShutdownUnref();
      Unref();
      return false;
    }
    ShutdownUnref();
    return false;
  }

  void TriggerShutdown() {
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        break;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr | kShutdownBit,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        Ref();
        if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
            kShutdownBit + 1) {
          grpc_call_cancel_internal(call_);
          Unref();
        }
        break;
      }
    }
    // Release the baseline reference owned by the wrapper.
    Unref();
  }

 private:
  bool ShutdownRef() {
    int64_t curr = shutdown_ref_.load(std::memory_order_acquire);
    while (true) {
      if (curr & kShutdownBit) {
        return false;
      }
      if (shutdown_ref_.compare_exchange_strong(curr, curr + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  void ShutdownUnref() {
    if (shutdown_ref_.fetch_sub(1, std::memory_order_acq_rel) ==
        kShutdownBit + 1) {
      grpc_call_cancel_internal(call_);
      Unref();
    }
  }

  grpc_call* const call_;
  const bool is_client_;

  std::atomic<int64_t> refs_{1};
  std::atomic<int64_t> shutdown_ref_{1};

  SessionEndpointTag read_tag_;
  grpc_byte_buffer* read_buffer_ = nullptr;
  std::atomic<bool> read_in_progress_{false};

  SessionEndpointTag write_tag_;
  std::atomic<bool> write_in_progress_{false};
};

grpc_endpoint* SessionEndpoint::Create(grpc_call* call, bool is_client) {
  auto endpoint = std::make_unique<SessionEndpoint>(call, is_client);
  return grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
      std::move(endpoint));
}

SessionEndpoint::SessionEndpoint(grpc_call* call, bool is_client)
    : impl_(new SessionEndpointImpl(call, is_client)) {}

SessionEndpoint::~SessionEndpoint() { impl_->TriggerShutdown(); }

bool SessionEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           grpc_event_engine::experimental::SliceBuffer* buffer,
                           ReadArgs /*args*/) {
  return impl_->Read(std::move(on_read), buffer);
}

bool SessionEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            grpc_event_engine::experimental::SliceBuffer* data,
                            WriteArgs /*args*/) {
  return impl_->Write(std::move(on_writable), data);
}

}  // namespace grpc_core
