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

#include <inttypes.h>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/lame_client.h"

namespace grpc_core {
namespace {

bool IsLameChannel(Channel* channel) {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(channel->channel_stack());
  return elem->filter == &LameClientFilter::kFilter;
}

}  // namespace
}  // namespace grpc_core

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel* c_channel, int try_to_connect) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (c_channel, try_to_connect));
  grpc_core::Channel* channel = grpc_core::Channel::FromC(c_channel);
  // Forward through to the underlying client channel.
  grpc_core::ClientChannelFilter* client_channel =
      grpc_core::ClientChannelFilter::GetFromChannel(channel);
  if (GPR_UNLIKELY(client_channel == nullptr)) {
    if (grpc_core::IsLameChannel(channel)) {
      return GRPC_CHANNEL_TRANSIENT_FAILURE;
    }
    gpr_log(GPR_ERROR,
            "grpc_channel_check_connectivity_state called on something that is "
            "not a client channel");
    return GRPC_CHANNEL_SHUTDOWN;
  }
  return client_channel->CheckConnectivityState(try_to_connect);
}

int grpc_channel_num_external_connectivity_watchers(grpc_channel* c_channel) {
  grpc_core::Channel* channel = grpc_core::Channel::FromC(c_channel);
  grpc_core::ClientChannelFilter* client_channel =
      grpc_core::ClientChannelFilter::GetFromChannel(channel);
  if (client_channel == nullptr) {
    if (!grpc_core::IsLameChannel(channel)) {
      gpr_log(GPR_ERROR,
              "grpc_channel_num_external_connectivity_watchers called on "
              "something that is not a client channel");
    }
    return 0;
  }
  return client_channel->NumExternalConnectivityWatchers();
}

int grpc_channel_support_connectivity_watcher(grpc_channel* channel) {
  return grpc_core::ClientChannelFilter::GetFromChannel(
             grpc_core::Channel::FromC(channel)) != nullptr;
}

namespace grpc_core {
namespace {

class StateWatcher : public DualRefCounted<StateWatcher> {
 public:
  StateWatcher(grpc_channel* c_channel, grpc_completion_queue* cq, void* tag,
               grpc_connectivity_state last_observed_state,
               gpr_timespec deadline)
      : channel_(Channel::FromC(c_channel)->Ref()),
        cq_(cq),
        tag_(tag),
        state_(last_observed_state) {
    GPR_ASSERT(grpc_cq_begin_op(cq, tag));
    GRPC_CLOSURE_INIT(&on_complete_, WatchComplete, this, nullptr);
    ClientChannelFilter* client_channel =
        ClientChannelFilter::GetFromChannel(channel_.get());
    if (client_channel == nullptr) {
      // If the target URI used to create the channel was invalid, channel
      // stack initialization failed, and that caused us to create a lame
      // channel.  In that case, connectivity state will never change (it
      // will always be TRANSIENT_FAILURE), so we don't actually start a
      // watch, but we are hiding that fact from the application.
      if (IsLameChannel(channel_.get())) {
        // A ref is held by the timer callback.
        StartTimer(Timestamp::FromTimespecRoundUp(deadline));
        // Ref from object creation needs to be freed here since lame channel
        // does not have a watcher.
        Unref();
        return;
      }
      Crash(
          "grpc_channel_watch_connectivity_state called on something that is "
          "not a client channel");
    }
    // Ref from object creation is held by the watcher callback.
    auto* watcher_timer_init_state = new WatcherTimerInitState(
        this, Timestamp::FromTimespecRoundUp(deadline));
    client_channel->AddExternalConnectivityWatcher(
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &state_,
        &on_complete_, watcher_timer_init_state->closure());
  }

 private:
  // A fire-and-forget object used to delay starting the timer until the
  // ClientChannelFilter actually starts the watch.
  class WatcherTimerInitState {
   public:
    WatcherTimerInitState(StateWatcher* state_watcher, Timestamp deadline)
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
    Timestamp deadline_;
    grpc_closure closure_;
  };

  void StartTimer(Timestamp deadline) {
    const Duration timeout = deadline - Timestamp::Now();
    MutexLock lock(&mu_);
    timer_handle_ = channel_->channel_stack()->EventEngine()->RunAfter(
        timeout, [self = Ref()]() mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->TimeoutComplete();
          // StateWatcher deletion might require an active ExecCtx.
          self.reset();
        });
  }

  static void WatchComplete(void* arg, grpc_error_handle error) {
    auto* self = static_cast<StateWatcher*>(arg);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_operation_failures)) {
      GRPC_LOG_IF_ERROR("watch_completion_error", error);
    }
    {
      MutexLock lock(&self->mu_);
      if (self->timer_handle_.has_value()) {
        self->channel_->channel_stack()->EventEngine()->Cancel(
            *self->timer_handle_);
      }
    }
    // Watcher fired when either notified or cancelled, either way the state of
    // this watcher has been cleared from the client channel. Thus there is no
    // need to cancel the watch again.
    self->Unref();
  }

  void TimeoutComplete() {
    timer_fired_ = true;
    // If this is a client channel (not a lame channel), cancel the watch.
    ClientChannelFilter* client_channel =
        ClientChannelFilter::GetFromChannel(channel_.get());
    if (client_channel != nullptr) {
      client_channel->CancelExternalConnectivityWatcher(&on_complete_);
    }
  }

  // Invoked when both strong refs are released.
  void Orphan() override {
    WeakRef().release();  // Take a weak ref until completion is finished.
    grpc_error_handle error =
        timer_fired_
            ? GRPC_ERROR_CREATE("Timed out waiting for connection state change")
            : absl::OkStatus();
    grpc_cq_end_op(cq_, tag_, error, FinishedCompletion, this,
                   &completion_storage_);
  }

  // Called when the completion is returned to the CQ.
  static void FinishedCompletion(void* arg, grpc_cq_completion* /*ignored*/) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->WeakUnref();
  }

  RefCountedPtr<Channel> channel_;
  grpc_completion_queue* cq_;
  void* tag_;

  grpc_connectivity_state state_;

  grpc_cq_completion completion_storage_;

  grpc_closure on_complete_;

  // timer_handle_ might be accessed in parallel from multiple threads, e.g.
  // timer callback fired immediately on an EventEngine thread before
  // RunAfter() returns.
  Mutex mu_;
  absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      timer_handle_ ABSL_GUARDED_BY(mu_);
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
