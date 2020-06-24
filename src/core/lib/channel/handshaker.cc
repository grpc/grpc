/*
 *
 * Copyright 2016 gRPC authors.
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
 *
 */

#include <grpc/support/port_platform.h>

#include <string.h>

#include "absl/strings/str_format.h"

#include <grpc/impl/codegen/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

TraceFlag grpc_handshaker_trace(false, "handshaker");

namespace {

std::string HandshakerArgsString(HandshakerArgs* args) {
  size_t num_args = args->args != nullptr ? args->args->num_args : 0;
  size_t read_buffer_length =
      args->read_buffer != nullptr ? args->read_buffer->length : 0;
  return absl::StrFormat(
      "{endpoint=%p, args=%p {size=%" PRIuPTR
      ": %s}, read_buffer=%p (length=%" PRIuPTR "), exit_early=%d}",
      args->endpoint, args->args, num_args,
      grpc_channel_args_string(args->args), args->read_buffer,
      read_buffer_length, args->exit_early);
}

}  // namespace

HandshakeManager::HandshakeManager() { gpr_mu_init(&mu_); }

/// Add \a mgr to the server side list of all pending handshake managers, the
/// list starts with \a *head.
// Not thread-safe. Caller needs to synchronize.
void HandshakeManager::AddToPendingMgrList(HandshakeManager** head) {
  GPR_ASSERT(prev_ == nullptr);
  GPR_ASSERT(next_ == nullptr);
  next_ = *head;
  if (*head) {
    (*head)->prev_ = this;
  }
  *head = this;
}

/// Remove \a mgr from the server side list of all pending handshake managers.
// Not thread-safe. Caller needs to synchronize.
void HandshakeManager::RemoveFromPendingMgrList(HandshakeManager** head) {
  if (next_ != nullptr) {
    next_->prev_ = prev_;
  }
  if (prev_ != nullptr) {
    prev_->next_ = next_;
  } else {
    GPR_ASSERT(*head == this);
    *head = next_;
  }
}

/// Shutdown all pending handshake managers starting at head on the server
/// side. Not thread-safe. Caller needs to synchronize.
void HandshakeManager::ShutdownAllPending(grpc_error* why) {
  auto* head = this;
  while (head != nullptr) {
    head->Shutdown(GRPC_ERROR_REF(why));
    head = head->next_;
  }
  GRPC_ERROR_UNREF(why);
}

void HandshakeManager::Add(RefCountedPtr<Handshaker> handshaker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
    gpr_log(
        GPR_INFO,
        "handshake_manager %p: adding handshaker %s [%p] at index %" PRIuPTR,
        this, handshaker->name(), handshaker.get(), handshakers_.size());
  }
  MutexLock lock(&mu_);
  handshakers_.push_back(std::move(handshaker));
}

HandshakeManager::~HandshakeManager() {
  handshakers_.clear();
  gpr_mu_destroy(&mu_);
}

void HandshakeManager::Shutdown(grpc_error* why) {
  {
    MutexLock lock(&mu_);
    // Shutdown the handshaker that's currently in progress, if any.
    if (!is_shutdown_ && index_ > 0) {
      is_shutdown_ = true;
      handshakers_[index_ - 1]->Shutdown(GRPC_ERROR_REF(why));
    }
  }
  GRPC_ERROR_UNREF(why);
}

// Helper function to call either the next handshaker or the
// on_handshake_done callback.
// Returns true if we've scheduled the on_handshake_done callback.
bool HandshakeManager::CallNextHandshakerLocked(grpc_error* error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
    gpr_log(GPR_INFO,
            "handshake_manager %p: error=%s shutdown=%d index=%" PRIuPTR
            ", args=%s",
            this, grpc_error_string(error), is_shutdown_, index_,
            HandshakerArgsString(&args_).c_str());
  }
  GPR_ASSERT(index_ <= handshakers_.size());
  // If we got an error or we've been shut down or we're exiting early or
  // we've finished the last handshaker, invoke the on_handshake_done
  // callback.  Otherwise, call the next handshaker.
  if (error != GRPC_ERROR_NONE || is_shutdown_ || args_.exit_early ||
      index_ == handshakers_.size()) {
    if (error == GRPC_ERROR_NONE && is_shutdown_) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("handshaker shutdown");
      // It is possible that the endpoint has already been destroyed by
      // a shutdown call while this callback was sitting on the ExecCtx
      // with no error.
      if (args_.endpoint != nullptr) {
        // TODO(roth): It is currently necessary to shutdown endpoints
        // before destroying then, even when we know that there are no
        // pending read/write callbacks.  This should be fixed, at which
        // point this can be removed.
        grpc_endpoint_shutdown(args_.endpoint, GRPC_ERROR_REF(error));
        grpc_endpoint_destroy(args_.endpoint);
        args_.endpoint = nullptr;
        grpc_channel_args_destroy(args_.args);
        args_.args = nullptr;
        grpc_slice_buffer_destroy_internal(args_.read_buffer);
        gpr_free(args_.read_buffer);
        args_.read_buffer = nullptr;
      }
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
      gpr_log(GPR_INFO,
              "handshake_manager %p: handshaking complete -- scheduling "
              "on_handshake_done with error=%s",
              this, grpc_error_string(error));
    }
    // Cancel deadline timer, since we're invoking the on_handshake_done
    // callback now.
    grpc_timer_cancel(&deadline_timer_);
    ExecCtx::Run(DEBUG_LOCATION, &on_handshake_done_, error);
    is_shutdown_ = true;
  } else {
    auto handshaker = handshakers_[index_];
    if (GRPC_TRACE_FLAG_ENABLED(grpc_handshaker_trace)) {
      gpr_log(
          GPR_INFO,
          "handshake_manager %p: calling handshaker %s [%p] at index %" PRIuPTR,
          this, handshaker->name(), handshaker.get(), index_);
    }
    handshaker->DoHandshake(acceptor_, &call_next_handshaker_, &args_);
  }
  ++index_;
  return is_shutdown_;
}

void HandshakeManager::CallNextHandshakerFn(void* arg, grpc_error* error) {
  auto* mgr = static_cast<HandshakeManager*>(arg);
  bool done;
  {
    MutexLock lock(&mgr->mu_);
    done = mgr->CallNextHandshakerLocked(GRPC_ERROR_REF(error));
  }
  // If we're invoked the final callback, we won't be coming back
  // to this function, so we can release our reference to the
  // handshake manager.
  if (done) {
    mgr->Unref();
  }
}

void HandshakeManager::OnTimeoutFn(void* arg, grpc_error* error) {
  auto* mgr = static_cast<HandshakeManager*>(arg);
  if (error == GRPC_ERROR_NONE) {  // Timer fired, rather than being cancelled
    mgr->Shutdown(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake timed out"));
  }
  mgr->Unref();
}

void HandshakeManager::DoHandshake(grpc_endpoint* endpoint,
                                   const grpc_channel_args* channel_args,
                                   grpc_millis deadline,
                                   grpc_tcp_server_acceptor* acceptor,
                                   grpc_iomgr_cb_func on_handshake_done,
                                   void* user_data) {
  bool done;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(index_ == 0);
    GPR_ASSERT(!is_shutdown_);
    // Construct handshaker args.  These will be passed through all
    // handshakers and eventually be freed by the on_handshake_done callback.
    args_.endpoint = endpoint;
    args_.args = grpc_channel_args_copy(channel_args);
    args_.user_data = user_data;
    args_.read_buffer =
        static_cast<grpc_slice_buffer*>(gpr_malloc(sizeof(*args_.read_buffer)));
    grpc_slice_buffer_init(args_.read_buffer);
    if (acceptor != nullptr && acceptor->external_connection &&
        acceptor->pending_data != nullptr) {
      grpc_slice_buffer_swap(args_.read_buffer,
                             &(acceptor->pending_data->data.raw.slice_buffer));
    }
    // Initialize state needed for calling handshakers.
    acceptor_ = acceptor;
    GRPC_CLOSURE_INIT(&call_next_handshaker_,
                      &HandshakeManager::CallNextHandshakerFn, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_handshake_done_, on_handshake_done, &args_,
                      grpc_schedule_on_exec_ctx);
    // Start deadline timer, which owns a ref.
    Ref().release();
    GRPC_CLOSURE_INIT(&on_timeout_, &HandshakeManager::OnTimeoutFn, this,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&deadline_timer_, deadline, &on_timeout_);
    // Start first handshaker, which also owns a ref.
    Ref().release();
    done = CallNextHandshakerLocked(GRPC_ERROR_NONE);
  }
  if (done) {
    Unref();
  }
}

}  // namespace grpc_core

void grpc_handshake_manager_add(grpc_handshake_manager* mgr,
                                grpc_handshaker* handshaker) {
  // This is a transition method to aid the API change for handshakers.
  using namespace grpc_core;
  RefCountedPtr<Handshaker> refd_hs(static_cast<Handshaker*>(handshaker));
  mgr->Add(refd_hs);
}
