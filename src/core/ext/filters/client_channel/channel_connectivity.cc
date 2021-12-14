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

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/lame_client.h"

namespace {

bool IsLameChannel(grpc_channel* channel) {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  return elem->filter == &grpc_lame_filter;
}

}  // namespace

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
    if (IsLameChannel(channel)) return GRPC_CHANNEL_TRANSIENT_FAILURE;
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
    if (!IsLameChannel(channel)) {
      gpr_log(GPR_ERROR,
              "grpc_channel_num_external_connectivity_watchers called on "
              "something that is not a client channel");
    }
    return 0;
  }
  return client_channel->NumExternalConnectivityWatchers();
}

int grpc_channel_support_connectivity_watcher(grpc_channel* channel) {
  return grpc_core::ClientChannel::GetFromChannel(channel) != nullptr;
}

namespace grpc_core {
namespace {

class StateWatcher : public DualRefCounted<StateWatcher> {
 public:
  StateWatcher(grpc_channel* channel, grpc_completion_queue* cq, void* tag,
               grpc_connectivity_state last_observed_state,
               gpr_timespec deadline)
      : channel_(channel), cq_(cq), tag_(tag), state_(last_observed_state) {
    GPR_ASSERT(grpc_cq_begin_op(cq, tag));
    GRPC_CHANNEL_INTERNAL_REF(channel, "watch_channel_connectivity");
    GRPC_CLOSURE_INIT(&on_complete_, WatchComplete, this, nullptr);
    GRPC_CLOSURE_INIT(&on_timeout_, TimeoutComplete, this, nullptr);
    ClientChannel* client_channel = ClientChannel::GetFromChannel(channel);
    if (client_channel == nullptr) {
      // If the target URI used to create the channel was invalid, channel
      // stack initialization failed, and that caused us to create a lame
      // channel.  In that case, connectivity state will never change (it
      // will always be TRANSIENT_FAILURE), so we don't actually start a
      // watch, but we are hiding that fact from the application.
      if (IsLameChannel(channel)) {
        // Ref from object creation is held by timer callback.
        StartTimer(grpc_timespec_to_millis_round_up(deadline));
        return;
      }
      gpr_log(GPR_ERROR,
              "grpc_channel_watch_connectivity_state called on "
              "something that is not a client channel");
      GPR_ASSERT(false);
    }
    // Take an addition ref, so we have two (the first one is from the
    // creation of this object).  One will be held by the timer callback,
    // the other by the watcher callback.
    Ref().release();
    auto* watcher_timer_init_state = new WatcherTimerInitState(
        this, grpc_timespec_to_millis_round_up(deadline));
    client_channel->AddExternalConnectivityWatcher(
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &state_,
        &on_complete_, watcher_timer_init_state->closure());
  }

  ~StateWatcher() override {
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
      self->state_watcher_->StartTimer(self->deadline_);
      delete self;
    }

    StateWatcher* state_watcher_;
    grpc_millis deadline_;
    grpc_closure closure_;
  };

  void StartTimer(grpc_millis deadline) {
    grpc_timer_init(&timer_, deadline, &on_timeout_);
  }

  static void WatchComplete(void* arg, grpc_error_handle error) {
    auto* self = static_cast<StateWatcher*>(arg);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_operation_failures)) {
      GRPC_LOG_IF_ERROR("watch_completion_error", GRPC_ERROR_REF(error));
    }
    grpc_timer_cancel(&self->timer_);
    self->Unref();
  }

  static void TimeoutComplete(void* arg, grpc_error_handle error) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->timer_fired_ = error == GRPC_ERROR_NONE;
    // If this is a client channel (not a lame channel), cancel the watch.
    ClientChannel* client_channel =
        ClientChannel::GetFromChannel(self->channel_);
    if (client_channel != nullptr) {
      client_channel->CancelExternalConnectivityWatcher(&self->on_complete_);
    }
    self->Unref();
  }

  // Invoked when both strong refs are released.
  void Orphan() override {
    WeakRef().release();  // Take a weak ref until completion is finished.
    grpc_error_handle error =
        timer_fired_ ? GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "Timed out waiting for connection state change")
                     : GRPC_ERROR_NONE;
    grpc_cq_end_op(cq_, tag_, error, FinishedCompletion, this,
                   &completion_storage_);
  }

  // Called when the completion is returned to the CQ.
  static void FinishedCompletion(void* arg, grpc_cq_completion* /*ignored*/) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->WeakUnref();
  }

  grpc_channel* channel_;
  grpc_completion_queue* cq_;
  void* tag_;

  grpc_connectivity_state state_;

  grpc_cq_completion completion_storage_;

  grpc_closure on_complete_;
  grpc_timer timer_;
  grpc_closure on_timeout_;

  bool timer_fired_ = false;
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
