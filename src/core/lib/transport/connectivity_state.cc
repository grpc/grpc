//
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
//

#include "src/core/lib/transport/connectivity_state.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

const char* ConnectivityStateName(grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "CONNECTING";
    case GRPC_CHANNEL_READY:
      return "READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

//
// AsyncConnectivityStateWatcherInterface
//

// A fire-and-forget class to asynchronously deliver a connectivity
// state notification to a watcher.
class AsyncConnectivityStateWatcherInterface::Notifier {
 public:
  Notifier(RefCountedPtr<AsyncConnectivityStateWatcherInterface> watcher,
           grpc_connectivity_state state, const absl::Status& status,
           const std::shared_ptr<WorkSerializer>& work_serializer)
      : watcher_(std::move(watcher)), state_(state), status_(status) {
    if (work_serializer != nullptr) {
      work_serializer->Run(
          [this]() { SendNotification(this, absl::OkStatus()); },
          DEBUG_LOCATION);
    } else {
      GRPC_CLOSURE_INIT(&closure_, SendNotification, this,
                        grpc_schedule_on_exec_ctx);
      ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
    }
  }

 private:
  static void SendNotification(void* arg, grpc_error_handle /*ignored*/) {
    Notifier* self = static_cast<Notifier*>(arg);
    if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
      gpr_log(GPR_INFO, "watcher %p: delivering async notification for %s (%s)",
              self->watcher_.get(), ConnectivityStateName(self->state_),
              self->status_.ToString().c_str());
    }
    self->watcher_->OnConnectivityStateChange(self->state_, self->status_);
    delete self;
  }

  RefCountedPtr<AsyncConnectivityStateWatcherInterface> watcher_;
  const grpc_connectivity_state state_;
  const absl::Status status_;
  grpc_closure closure_;
};

void AsyncConnectivityStateWatcherInterface::Notify(
    grpc_connectivity_state state, const absl::Status& status) {
  // Deletes itself when done.
  new Notifier(RefAsSubclass<AsyncConnectivityStateWatcherInterface>(), state,
               status, work_serializer_);
}

//
// ConnectivityStateTracker
//

ConnectivityStateTracker::~ConnectivityStateTracker() {
  grpc_connectivity_state current_state =
      state_.load(std::memory_order_relaxed);
  if (current_state == GRPC_CHANNEL_SHUTDOWN) return;
  for (const auto& p : watchers_) {
    if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
      gpr_log(GPR_INFO,
              "ConnectivityStateTracker %s[%p]: notifying watcher %p: %s -> %s",
              name_, this, p.first, ConnectivityStateName(current_state),
              ConnectivityStateName(GRPC_CHANNEL_SHUTDOWN));
    }
    p.second->Notify(GRPC_CHANNEL_SHUTDOWN, absl::Status());
  }
}

void ConnectivityStateTracker::AddWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
    gpr_log(GPR_INFO, "ConnectivityStateTracker %s[%p]: add watcher %p", name_,
            this, watcher.get());
  }
  grpc_connectivity_state current_state =
      state_.load(std::memory_order_relaxed);
  if (initial_state != current_state) {
    if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
      gpr_log(GPR_INFO,
              "ConnectivityStateTracker %s[%p]: notifying watcher %p: %s -> %s",
              name_, this, watcher.get(), ConnectivityStateName(initial_state),
              ConnectivityStateName(current_state));
    }
    watcher->Notify(current_state, status_);
  }
  // If we're in state SHUTDOWN, don't add the watcher, so that it will
  // be orphaned immediately.
  if (current_state != GRPC_CHANNEL_SHUTDOWN) {
    watchers_.insert(std::make_pair(watcher.get(), std::move(watcher)));
  }
}

void ConnectivityStateTracker::RemoveWatcher(
    ConnectivityStateWatcherInterface* watcher) {
  if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
    gpr_log(GPR_INFO, "ConnectivityStateTracker %s[%p]: remove watcher %p",
            name_, this, watcher);
  }
  watchers_.erase(watcher);
}

void ConnectivityStateTracker::SetState(grpc_connectivity_state state,
                                        const absl::Status& status,
                                        const char* reason) {
  grpc_connectivity_state current_state =
      state_.load(std::memory_order_relaxed);
  if (state == current_state) return;
  if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
    gpr_log(GPR_INFO, "ConnectivityStateTracker %s[%p]: %s -> %s (%s, %s)",
            name_, this, ConnectivityStateName(current_state),
            ConnectivityStateName(state), reason, status.ToString().c_str());
  }
  state_.store(state, std::memory_order_relaxed);
  status_ = status;
  for (const auto& p : watchers_) {
    if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
      gpr_log(GPR_INFO,
              "ConnectivityStateTracker %s[%p]: notifying watcher %p: %s -> %s",
              name_, this, p.first, ConnectivityStateName(current_state),
              ConnectivityStateName(state));
    }
    p.second->Notify(state, status);
  }
  // If the new state is SHUTDOWN, orphan all of the watchers.  This
  // avoids the need for the callers to explicitly cancel them.
  if (state == GRPC_CHANNEL_SHUTDOWN) watchers_.clear();
}

grpc_connectivity_state ConnectivityStateTracker::state() const {
  grpc_connectivity_state state = state_.load(std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(connectivity_state)) {
    gpr_log(GPR_INFO, "ConnectivityStateTracker %s[%p]: get current state: %s",
            name_, this, ConnectivityStateName(state));
  }
  return state;
}

}  // namespace grpc_core
