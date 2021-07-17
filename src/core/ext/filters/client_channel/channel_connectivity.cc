//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel* channel, int try_to_connect) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (channel, try_to_connect));
  // Forward through to the underlying client channel.
  grpc_core::ClientChannel* client_channel =
      grpc_core::ClientChannel::GetFromChannel(channel);
  if (GPR_UNLIKELY(client_channel == nullptr)) {
    gpr_log(GPR_ERROR,
            "grpc_channel_check_connectivity_state called on something that is "
            "not a client channel");
    return GRPC_CHANNEL_SHUTDOWN;
  }
  return client_channel->CheckConnectivityState(try_to_connect);
}

int grpc_channel_num_external_connectivity_watchers(grpc_channel* channel) {
  grpc_core::ClientChannel* client_channel =
      grpc_core::ClientChannel::GetFromChannel(channel);
  if (client_channel == nullptr) {
    gpr_log(GPR_ERROR,
            "grpc_channel_num_external_connectivity_watchers called on "
            "something that is not a client channel");
    return 0;
  }
  return client_channel->NumExternalConnectivityWatchers();
}

int grpc_channel_support_connectivity_watcher(grpc_channel* channel) {
  return grpc_core::ClientChannel::GetFromChannel(channel) != nullptr;
}

namespace grpc_core {
namespace {

class StateWatcher {
 public:
  StateWatcher(grpc_channel* channel, grpc_completion_queue* cq, void* tag,
               grpc_connectivity_state last_observed_state,
               gpr_timespec deadline)
      : channel_(channel), cq_(cq), tag_(tag), state_(last_observed_state) {
    GPR_ASSERT(grpc_cq_begin_op(cq, tag));
    GRPC_CHANNEL_INTERNAL_REF(channel, "watch_channel_connectivity");
    GRPC_CLOSURE_INIT(&on_complete_, WatchComplete, this, nullptr);
    GRPC_CLOSURE_INIT(&on_timeout_, TimeoutComplete, this, nullptr);
    auto* watcher_timer_init_state = new WatcherTimerInitState(
        this, grpc_timespec_to_millis_round_up(deadline));
    ClientChannel* client_channel = ClientChannel::GetFromChannel(channel);
    GPR_ASSERT(client_channel != nullptr);
    client_channel->AddExternalConnectivityWatcher(
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &state_,
        &on_complete_, watcher_timer_init_state->closure());
  }

  ~StateWatcher() {
    GRPC_CHANNEL_INTERNAL_UNREF(channel_, "watch_channel_connectivity");
  }

 private:
  // A fire-and-forget object used to delay starting the timer until the
  // ClientChannel actually starts the watch.
  class WatcherTimerInitState {
   public:
    WatcherTimerInitState(StateWatcher* state_watcher, grpc_millis deadline)
        : state_watcher_(state_watcher), deadline_(deadline) {
      GRPC_CLOSURE_INIT(&closure_, WatcherTimerInit, this, nullptr);
    }

    grpc_closure* closure() { return &closure_; }

   private:
    static void WatcherTimerInit(void* arg, grpc_error_handle /*error*/) {
      auto* self = static_cast<WatcherTimerInitState*>(arg);
      grpc_timer_init(&self->state_watcher_->timer_, self->deadline_,
                      &self->state_watcher_->on_timeout_);
      delete self;
    }

    StateWatcher* state_watcher_;
    grpc_millis deadline_;
    grpc_closure closure_;
  };

  enum CallbackPhase { kWaiting, kReadyToCallBack, kCallingBackAndFinished };

  // Called when the completion is returned to the CQ.
  static void FinishedCompletion(void* arg, grpc_cq_completion* /*ignored*/) {
    auto* self = static_cast<StateWatcher*>(arg);
    bool should_delete = false;
    {
      MutexLock lock(&self->mu_);
      switch (self->phase_) {
        case kWaiting:
        case kReadyToCallBack:
          GPR_UNREACHABLE_CODE(return );
        case kCallingBackAndFinished:
          should_delete = true;
      }
    }
    if (should_delete) delete self;
  }

  void PartlyDone(bool due_to_completion, grpc_error_handle error) {
    bool end_op = false;
    void* end_op_tag = nullptr;
    grpc_error_handle end_op_error = GRPC_ERROR_NONE;
    grpc_completion_queue* end_op_cq = nullptr;
    grpc_cq_completion* end_op_completion_storage = nullptr;
    if (due_to_completion) {
      grpc_timer_cancel(&timer_);
    } else {
      grpc_core::ClientChannel* client_channel =
          grpc_core::ClientChannel::GetFromChannel(channel_);
      GPR_ASSERT(client_channel != nullptr);
      client_channel->CancelExternalConnectivityWatcher(&on_complete_);
    }
    {
      MutexLock lock(&mu_);
      if (due_to_completion) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_operation_failures)) {
          GRPC_LOG_IF_ERROR("watch_completion_error", GRPC_ERROR_REF(error));
        }
        GRPC_ERROR_UNREF(error);
        error = GRPC_ERROR_NONE;
      } else {
        if (error == GRPC_ERROR_NONE) {
          error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Timed out waiting for connection state change");
        } else if (error == GRPC_ERROR_CANCELLED) {
          error = GRPC_ERROR_NONE;
        }
      }
      switch (phase_) {
        case kWaiting:
          GRPC_ERROR_REF(error);
          error_ = error;
          phase_ = kReadyToCallBack;
          break;
        case kReadyToCallBack:
          if (error != GRPC_ERROR_NONE) {
            GPR_ASSERT(!due_to_completion);
            GRPC_ERROR_UNREF(error_);
            GRPC_ERROR_REF(error);
            error_ = error;
          }
          phase_ = kCallingBackAndFinished;
          end_op = true;
          end_op_cq = cq_;
          end_op_tag = tag_;
          end_op_error = error_;
          end_op_completion_storage = &completion_storage_;
          break;
        case kCallingBackAndFinished:
          GPR_UNREACHABLE_CODE(return );
      }
    }
    if (end_op) {
      grpc_cq_end_op(end_op_cq, end_op_tag, end_op_error, FinishedCompletion,
                     this, end_op_completion_storage);
    }
    GRPC_ERROR_UNREF(error);
  }

  static void WatchComplete(void* arg, grpc_error_handle error) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->PartlyDone(/*due_to_completion=*/true, GRPC_ERROR_REF(error));
  }

  static void TimeoutComplete(void* arg, grpc_error_handle error) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->PartlyDone(/*due_to_completion=*/false, GRPC_ERROR_REF(error));
  }

  grpc_channel* channel_;
  grpc_completion_queue* cq_;
  void* tag_;

  grpc_connectivity_state state_;

  grpc_cq_completion completion_storage_;

  grpc_closure on_complete_;
  grpc_timer timer_;
  grpc_closure on_timeout_;

  Mutex mu_;
  CallbackPhase phase_ ABSL_GUARDED_BY(mu_) = kWaiting;
  grpc_error_handle error_ ABSL_GUARDED_BY(mu_) = GRPC_ERROR_NONE;
};

}  // namespace
}  // namespace grpc_core

void grpc_channel_watch_connectivity_state(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state("
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7,
      (channel, (int)last_observed_state, deadline.tv_sec, deadline.tv_nsec,
       (int)deadline.clock_type, cq, tag));
  new grpc_core::StateWatcher(channel, cq, tag, last_observed_state, deadline);
}
