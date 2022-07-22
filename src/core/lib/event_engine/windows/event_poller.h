// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_EVENT_POLLER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_EVENT_POLLER_H

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

// ---------------------------------------------------------------------------
// TODO(hork): I forked this from the iomgr_engine due to some slight
// differences in types. Both are being actively worked on, so the idea is to
// develop in parallel and netogiate a reunification once things settle.
//
// This is most valuable if WrappedSocket/EventHandler and EventPoller can be
// treated generally by the same TCP code. If not, unification might not be that
// valuable.
// ---------------------------------------------------------------------------


#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

class WrappedSocket {
 public:
  virtual SOCKET Socket() = 0;
  // Shutdown a WrappedSocket. After this operation, NotifyXXX and SetXXX
  // operations cannot be performed.
  virtual void MaybeShutdown(absl::Status why) = 0;
  // Schedule on_read to be invoked when the underlying socket
  // becomes readable.
  virtual void NotifyOnRead(absl::AnyInvocable<void()> on_read) = 0;
  // Schedule on_write to be invoked when the underlying socket
  // becomes writable.
  virtual void NotifyOnWrite(absl::AnyInvocable<void()> on_write) = 0;
  // Force set a readable event on the underlying socket.
  virtual void SetReadable() = 0;
  // Force set a writable event on the underlying socket.
  virtual void SetWritable() = 0;
  // Returns true if the WrappedSocket has been shutdown.
  virtual bool IsShutdown() = 0;
  virtual ~WrappedSocket() = default;
};

// A low-level poller that watches for socket events and dispatches
// notifications.
class EventPoller {
 public:
  // Return an opaque WrappedSocket to perform actions on the provided socket.
  virtual WrappedSocket* Watch(SOCKET socket) = 0;
  // Shuts down and deletes the poller. It is legal to call this function
  // only when no other poller method is in progress. For instance, it is
  // not safe to call this method, while a thread is blocked on Work(...).
  // A graceful way to terminate the poller could be to:
  // 1. First shut down all created WrappedSockets.
  // 2. Send a Kick() to the threads executing Work(...) and wait for the
  //    threads to return.
  // 3. Call Shutdown() on the poller.
  virtual void Shutdown() = 0;
  // Poll all watched sockets for the specified period.
  // Implementations should deal with socket events in the following ways:
  // - if a closure is already-primed for notification, execute the callback
  // - if no closure is primed, set a flag to ensure that when some caller
  //   registers for notification, they will trigger execution of the callback
  //   immediately themselves
  virtual absl::Status Work(EventEngine::Duration timeout) = 0;
  // Trigger the threads executing Work(..) to break out as soon as possible.
  // This function is useful in tests. It may also be used to break a thread
  // out of Work(...) before calling Shutdown() on the poller.
  virtual void Kick() = 0;
  virtual ~EventPoller() = default;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS


#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_EVENT_POLLER_H
