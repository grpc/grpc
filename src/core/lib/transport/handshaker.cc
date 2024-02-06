//
//
// Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/handshaker.h"

#include <inttypes.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"

using ::grpc_event_engine::experimental::EventEngine;

namespace grpc_core {

TraceFlag grpc_handshaker_trace(false, "handshaker");

void Handshaker::InvokeOnHandshakeDone(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done,
    absl::Status status) {
  args->event_engine->Run([on_handshake_done = std::move(on_handshake_done),
                           status = std::move(status)]() mutable {
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    on_handshake_done(std::move(status));
    // Destroy callback while ExecCtx is still in scope.
    on_handshake_done = nullptr;
  });
}

namespace {

std::string HandshakerArgsString(HandshakerArgs* args) {
  return absl::StrFormat("{endpoint=%p, args=%s, read_buffer.Length()=%" PRIuPTR
                         ", exit_early=%d}",
                         args->endpoint, args->args.ToString(),
                         args->read_buffer.Length(), args->exit_early);
}

}  // namespace

HandshakeManager::HandshakeManager()
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)
                     ? "HandshakeManager"
                     : nullptr) {}

void HandshakeManager::Add(RefCountedPtr<Handshaker> handshaker) {
  MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
    gpr_log(
        GPR_INFO,
        "handshake_manager %p: adding handshaker %s [%p] at index %" PRIuPTR,
        this, std::string(handshaker->name()).c_str(), handshaker.get(),
        handshakers_.size());
  }
  handshakers_.push_back(std::move(handshaker));
}

void HandshakeManager::DoHandshake(
    grpc_endpoint* endpoint, const ChannelArgs& channel_args,
    Timestamp deadline, grpc_tcp_server_acceptor* acceptor,
    absl::AnyInvocable<void(absl::StatusOr<HandshakerArgs*>)>
        on_handshake_done) {
  bool done;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(index_ == 0);
    on_handshake_done_ = std::move(on_handshake_done);
    // Construct handshaker args.  These will be passed through all
    // handshakers and eventually be freed by the on_handshake_done callback.
    args_.endpoint = endpoint;
    args_.deadline = deadline;
    args_.args = channel_args;
    args_.event_engine = args_.args.GetObjectRef<EventEngine>();
    args_.acceptor = acceptor;
    if (acceptor != nullptr && acceptor->external_connection &&
        acceptor->pending_data != nullptr) {
      grpc_slice_buffer_swap(args_.read_buffer.c_slice_buffer(),
                             &(acceptor->pending_data->data.raw.slice_buffer));
      // TODO(vigneshbabu): For connections accepted through event engine
      // listeners, the ownership of the byte buffer received is transferred to
      // this callback and it is thus this callback's duty to delete it.
      // Make this hack default once event engine is rolled out.
      if (grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
              endpoint)) {
        grpc_byte_buffer_destroy(acceptor->pending_data);
      }
    }
    // Start deadline timer, which owns a ref.
    const Duration time_to_deadline = deadline - Timestamp::Now();
    deadline_timer_handle_ = args_.event_engine->RunAfter(
        time_to_deadline, [self = Ref()]() mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->Shutdown(GRPC_ERROR_CREATE("Handshake timed out"));
          // HandshakeManager deletion might require an active ExecCtx.
          self.reset();
        });
    // Start first handshaker, which also owns a ref.
    Ref().release();
    done = CallNextHandshakerLocked(absl::OkStatus());
  }
  if (done) Unref();
}

void HandshakeManager::Shutdown(absl::Status error) {
  MutexLock lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    // Shutdown the handshaker that's currently in progress, if any.
    if (index_ > 0) handshakers_[index_ - 1]->Shutdown(std::move(error));
  }
}

void HandshakeManager::CallNextHandshaker(absl::Status error) {
  bool done;
  {
    MutexLock lock(&mu_);
    done = CallNextHandshakerLocked(std::move(error));
  }
  // If we're invoked the final callback, we won't be coming back
  // to this function, so we can release our reference to the
  // handshake manager.
  if (done) Unref();
}

// Helper function to call either the next handshaker or the
// on_handshake_done callback.
// Returns true if we've scheduled the on_handshake_done callback.
bool HandshakeManager::CallNextHandshakerLocked(absl::Status error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
    gpr_log(GPR_INFO,
            "handshake_manager %p: error=%s shutdown=%d index=%" PRIuPTR
            ", args=%s",
            this, error.ToString().c_str(), is_shutdown_, index_,
            HandshakerArgsString(&args_).c_str());
  }
  GPR_ASSERT(index_ <= handshakers_.size());
  // If we got an error or we've been shut down or we're exiting early or
  // we've finished the last handshaker, invoke the on_handshake_done
  // callback.
  if (!error.ok() || is_shutdown_ || args_.exit_early ||
      index_ == handshakers_.size()) {
    if (error.ok() && is_shutdown_) {
      error = absl::CancelledError("handshaker shutdown");
      // It is possible that the endpoint has already been destroyed by
      // a shutdown call while this callback was sitting on the ExecCtx
      // with no error.
      if (args_.endpoint != nullptr) {
        // TODO(roth): It is currently necessary to shutdown endpoints
        // before destroying then, even when we know that there are no
        // pending read/write callbacks.  This should be fixed, at which
        // point this can be removed.
        grpc_endpoint_shutdown(args_.endpoint, error);
        grpc_endpoint_destroy(args_.endpoint);
        args_.endpoint = nullptr;
      }
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
      gpr_log(GPR_INFO,
              "handshake_manager %p: handshaking complete -- scheduling "
              "on_handshake_done with error=%s",
              this, error.ToString().c_str());
    }
    // Cancel deadline timer, since we're invoking the on_handshake_done
    // callback now.
    args_.event_engine->Cancel(deadline_timer_handle_);
    is_shutdown_ = true;
    absl::StatusOr<HandshakerArgs*> result(&args_);
    if (!error.ok()) result = std::move(error);
    args_.event_engine->Run([on_handshake_done = std::move(on_handshake_done_),
                             result = std::move(result)]() mutable {
      ApplicationCallbackExecCtx callback_exec_ctx;
      ExecCtx exec_ctx;
      on_handshake_done(std::move(result));
      // Destroy callback while ExecCtx is still in scope.
      on_handshake_done = nullptr;
    });
    return true;
  }
  // Call the next handshaker.
  auto handshaker = handshakers_[index_];
  if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
    gpr_log(
        GPR_INFO,
        "handshake_manager %p: calling handshaker %s [%p] at index %" PRIuPTR,
        this, std::string(handshaker->name()).c_str(), handshaker.get(),
        index_);
  }
  ++index_;
  handshaker->DoHandshake(&args_, [this](absl::Status error) mutable {
    CallNextHandshaker(std::move(error));
  });
  return false;
}

}  // namespace grpc_core
