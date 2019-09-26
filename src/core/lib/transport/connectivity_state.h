/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H
#define GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/closure.h"

namespace grpc_core {

extern TraceFlag grpc_connectivity_state_trace;

// Enum to string conversion.
const char* ConnectivityStateName(grpc_connectivity_state state);

// Interface for watching connectivity state.
// Subclasses must implement the Notify() method.
//
// Note: Most callers will want to use
// AsyncConnectivityStateWatcherInterface instead.
class ConnectivityStateWatcherInterface
    : public InternallyRefCounted<ConnectivityStateWatcherInterface> {
 public:
  virtual ~ConnectivityStateWatcherInterface() = default;

  // Notifies the watcher that the state has changed to new_state.
  virtual void Notify(grpc_connectivity_state new_state) GRPC_ABSTRACT;

  void Orphan() override { Unref(); }

  GRPC_ABSTRACT_BASE_CLASS
};

// An alternative watcher interface that performs notifications via an
// asynchronous callback scheduled on the ExecCtx.
// Subclasses must implement the OnConnectivityStateChange() method.
class AsyncConnectivityStateWatcherInterface
    : public ConnectivityStateWatcherInterface {
 public:
  virtual ~AsyncConnectivityStateWatcherInterface() = default;

  // Schedules a closure on the ExecCtx to invoke
  // OnConnectivityStateChange() asynchronously.
  void Notify(grpc_connectivity_state new_state) override final;

 protected:
  class Notifier;

  // Invoked asynchronously when Notify() is called.
  virtual void OnConnectivityStateChange(grpc_connectivity_state new_state)
      GRPC_ABSTRACT;
};

// Tracks connectivity state.  Maintains a list of watchers that are
// notified whenever the state changes.
class ConnectivityStateTracker {
 public:
  ConnectivityStateTracker(const char* name,
                           grpc_connectivity_state state = GRPC_CHANNEL_IDLE)
      : name_(name), state_(state) {}

  ~ConnectivityStateTracker();

  // Adds a watcher.
  // If the current state is different than initial_state, the watcher
  // will be notified immediately.  Otherwise, it will be notified
  // whenever the state changes.
  // Not thread safe; access must be serialized with an external lock.
  void AddWatcher(grpc_connectivity_state initial_state,
                  OrphanablePtr<ConnectivityStateWatcherInterface> watcher);

  // Removes a watcher.  The watcher will be orphaned.
  // Not thread safe; access must be serialized with an external lock.
  void RemoveWatcher(ConnectivityStateWatcherInterface* watcher);

  // Sets connectivity state.
  // Not thread safe; access must be serialized with an external lock.
  void SetState(grpc_connectivity_state state, const char* reason);

  // Gets the current state.
  // Thread safe; no need to use an external lock.
  grpc_connectivity_state state() const;

 private:
  const char* name_;
  Atomic<grpc_connectivity_state> state_;
  // TODO(roth): This could be a set instead of a map if we had a set
  // implementation.
  Map<ConnectivityStateWatcherInterface*,
      OrphanablePtr<ConnectivityStateWatcherInterface>>
      watchers_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H */
