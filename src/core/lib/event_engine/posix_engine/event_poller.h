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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EVENT_POLLER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EVENT_POLLER_H
#include <grpc/support/port_platform.h>

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace posix_engine {

class Scheduler {
 public:
  virtual void Run(experimental::EventEngine::Closure* closure) = 0;
  virtual ~Scheduler() = default;
};

class EventHandle {
 public:
  virtual int WrappedFd() = 0;
  // Delete the handle and optionally close the underlying file descriptor if
  // release_fd != nullptr. The on_done closure is scheduled to be invoked
  // after the operation is complete. After this operation, NotifyXXX and SetXXX
  // operations cannot be performed on the handle.
  virtual void OrphanHandle(IomgrEngineClosure* on_done, int* release_fd,
                            absl::string_view reason) = 0;
  // Shutdown a handle. After this operation, NotifyXXX and SetXXX operations
  // cannot be performed.
  virtual void ShutdownHandle(absl::Status why) = 0;
  // Schedule on_read to be invoked when the underlying file descriptor
  // becomes readable.
  virtual void NotifyOnRead(IomgrEngineClosure* on_read) = 0;
  // Schedule on_write to be invoked when the underlying file descriptor
  // becomes writable.
  virtual void NotifyOnWrite(IomgrEngineClosure* on_write) = 0;
  // Schedule on_error to be invoked when the underlying file descriptor
  // encounters errors.
  virtual void NotifyOnError(IomgrEngineClosure* on_error) = 0;
  // Force set a readable event on the underlying file descriptor.
  virtual void SetReadable() = 0;
  // Force set a writable event on the underlying file descriptor.
  virtual void SetWritable() = 0;
  // Force set a error event on the underlying file descriptor.
  virtual void SetHasError() = 0;
  // Returns true if the handle has been shutdown.
  virtual bool IsHandleShutdown() = 0;
  // Execute any pending actions that may have been set to a handle after the
  // last invocation of Work(...) function.
  virtual void ExecutePendingActions() = 0;
  virtual ~EventHandle() = default;
};

class EventPoller {
 public:
  // Return an opaque handle to perform actions on the provided file descriptor.
  virtual EventHandle* CreateHandle(int fd, absl::string_view name,
                                    bool track_err) = 0;
  // Shuts down and deletes the poller. It is legal to call this function
  // only when no other poller method is in progress. For instance, it is
  // not safe to call this method, while a thread is blocked on Work(...).
  // A graceful way to terminate the poller could be to:
  // 1. First orphan all created handles.
  // 2. Send a Kick() to the thread executing Work(...) and wait for the
  //    thread to return.
  // 3. Call Shutdown() on the poller.
  virtual void Shutdown() = 0;
  // Poll all the underlying file descriptors for the specified period
  // and return a vector containing a list of handles which have pending
  // events. The calling thread should invoke ExecutePendingActions on each
  // returned handle to take the necessary pending actions. Only one thread
  // may invoke the Work function at any given point in time. The Work(...)
  // method returns an absl Non-OK status if it was Kicked.
  virtual absl::Status Work(grpc_core::Timestamp deadline,
                            std::vector<EventHandle*>& pending_events) = 0;
  // Trigger the thread executing Work(..) to break out as soon as possible.
  // This function is useful in tests. It may also be used to break a thread
  // out of Work(...) before calling Shutdown() on the poller.
  virtual void Kick() = 0;
  virtual ~EventPoller() = default;
};

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EVENT_POLLER_H