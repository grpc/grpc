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

#include "src/core/handshaker/handshaker.h"

#include <inttypes.h>

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

namespace {

using ::grpc_event_engine::experimental::EventEngine;

std::string HandshakerArgsString(HandshakerArgs* args) {
  size_t read_buffer_length =
      args->read_buffer != nullptr ? args->read_buffer->length : 0;
  return absl::StrFormat(
      "{endpoint=%p, args=%s, read_buffer=%p (length=%" PRIuPTR
      "), exit_early=%d}",
      args->endpoint, args->args.ToString(), args->read_buffer,
      read_buffer_length, args->exit_early);
}

}  // namespace

HandshakeManager::HandshakeManager()
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(handshaker) ? "HandshakeManager"
                                                     : nullptr) {}

void HandshakeManager::Add(RefCountedPtr<Handshaker> handshaker) {
  MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(handshaker)) {
    LOG(INFO) << "handshake_manager " << this << ": adding handshaker "
              << std::string(handshaker->name()) << " [" << handshaker.get()
              << "] at index " << handshakers_.size();
  }
  handshakers_.push_back(std::move(handshaker));
}

HandshakeManager::~HandshakeManager() { handshakers_.clear(); }

void HandshakeManager::Shutdown(grpc_error_handle why) {
  MutexLock lock(&mu_);
  // Shutdown the handshaker that's currently in progress, if any.
  if (!is_shutdown_ && index_ > 0) {
    GRPC_TRACE_LOG(handshaker, INFO)
        << "handshake_manager " << this << ": Shutdown() called: " << why
        << ". shutting down handshaker at index " << index_ - 1;
    // Shutdown the handshaker that's currently in progress, if any.
    is_shutdown_ = true;
    handshakers_[index_ - 1]->Shutdown(std::move(why));
  }
}

// Helper function to call either the next handshaker or the
// on_handshake_done callback.
// Returns true if we've scheduled the on_handshake_done callback.
bool HandshakeManager::CallNextHandshakerLocked(grpc_error_handle error) {
  if (GRPC_TRACE_FLAG_ENABLED(handshaker)) {
    LOG(INFO) << "handshake_manager " << this << ": error=" << error
              << " shutdown=" << is_shutdown_ << " index=" << index_
              << ", args=" << HandshakerArgsString(&args_);
  }
  CHECK(index_ <= handshakers_.size());
  // If we got an error or we've been shut down or we're exiting early or
  // we've finished the last handshaker, invoke the on_handshake_done
  // callback.  Otherwise, call the next handshaker.
  if (!error.ok() || is_shutdown_ || args_.exit_early ||
      index_ == handshakers_.size()) {
    if (error.ok() && is_shutdown_) {
      error = GRPC_ERROR_CREATE("handshaker shutdown");
      // It is possible that the endpoint has already been destroyed by
      // a shutdown call while this callback was sitting on the ExecCtx
      // with no error.
      if (args_.endpoint != nullptr) {
        grpc_endpoint_destroy(args_.endpoint);
        args_.endpoint = nullptr;
      }
      if (args_.read_buffer != nullptr) {
        grpc_slice_buffer_destroy(args_.read_buffer);
        gpr_free(args_.read_buffer);
        args_.read_buffer = nullptr;
      }
      args_.args = ChannelArgs();
    }
    if (GRPC_TRACE_FLAG_ENABLED(handshaker)) {
      LOG(INFO) << "handshake_manager " << this
                << ": handshaking complete -- scheduling "
                   "on_handshake_done with error="
                << error;
    }
    // Cancel deadline timer, since we're invoking the on_handshake_done
    // callback now.
    event_engine_->Cancel(deadline_timer_handle_);
    ExecCtx::Run(DEBUG_LOCATION, &on_handshake_done_, error);
    is_shutdown_ = true;
  } else {
    auto handshaker = handshakers_[index_];
    if (GRPC_TRACE_FLAG_ENABLED(handshaker)) {
      LOG(INFO) << "handshake_manager " << this << ": calling handshaker "
                << handshaker->name() << " [" << handshaker.get()
                << "] at index " << index_;
    }
    handshaker->DoHandshake(acceptor_, &call_next_handshaker_, &args_);
  }
  ++index_;
  return is_shutdown_;
}

void HandshakeManager::CallNextHandshakerFn(void* arg,
                                            grpc_error_handle error) {
  auto* mgr = static_cast<HandshakeManager*>(arg);
  bool done;
  {
    MutexLock lock(&mgr->mu_);
    done = mgr->CallNextHandshakerLocked(error);
  }
  // If we're invoked the final callback, we won't be coming back
  // to this function, so we can release our reference to the
  // handshake manager.
  if (done) {
    mgr->Unref();
  }
}

void HandshakeManager::DoHandshake(grpc_endpoint* endpoint,
                                   const ChannelArgs& channel_args,
                                   Timestamp deadline,
                                   grpc_tcp_server_acceptor* acceptor,
                                   grpc_iomgr_cb_func on_handshake_done,
                                   void* user_data) {
  bool done;
  {
    MutexLock lock(&mu_);
    CHECK_EQ(index_, 0u);
    // Construct handshaker args.  These will be passed through all
    // handshakers and eventually be freed by the on_handshake_done callback.
    args_.endpoint = endpoint;
    args_.deadline = deadline;
    args_.args = channel_args;
    args_.user_data = user_data;
    args_.read_buffer =
        static_cast<grpc_slice_buffer*>(gpr_malloc(sizeof(*args_.read_buffer)));
    grpc_slice_buffer_init(args_.read_buffer);
    if (acceptor != nullptr && acceptor->external_connection &&
        acceptor->pending_data != nullptr) {
      grpc_slice_buffer_swap(args_.read_buffer,
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
    // Initialize state needed for calling handshakers.
    acceptor_ = acceptor;
    GRPC_CLOSURE_INIT(&call_next_handshaker_,
                      &HandshakeManager::CallNextHandshakerFn, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_handshake_done_, on_handshake_done, &args_,
                      grpc_schedule_on_exec_ctx);
    // Start deadline timer, which owns a ref.
    const Duration time_to_deadline = deadline - Timestamp::Now();
    event_engine_ = args_.args.GetObjectRef<EventEngine>();
    deadline_timer_handle_ =
        event_engine_->RunAfter(time_to_deadline, [self = Ref()]() mutable {
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
  if (done) {
    Unref();
  }
}

}  // namespace grpc_core
