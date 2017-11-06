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

#ifndef GRPC_CORE_LIB_IOMGR_POLLER_H
#define GRPC_CORE_LIB_IOMGR_POLLER_H

#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/exec_ctx.h"

#ifndef NDEBUG
extern grpc_tracer_flag grpc_trace_fd_refcount;
#endif

namespace grpc_core {

// Forward declarations
class PollingJoin;

// A Pollable is an object that can be polled
// Polling engines derive from this interface and implement
// This interface should not be implemented outside of a polling engine
class Pollable {};

// A PollableCollection is a front-end interface to Poller & PollingJoin,
// collecting common methods.
// In many situations we don't care which we're dealing with, and so this class
// serves to abstract away that detail
class PollableCollection {
 public:
  // Add a Pollable to this collection
  virtual void AddPollable(Pollable* pollable) = 0;

 protected:
  PollableCollection() {}
  ~PollableCollection() {}

 private:
  friend class PollingJoin;
  virtual void AddToPollingJoin(PollingJoin* join) = 0;
  virtual void RemoveFromPollingJoin(PollingJoin* join) = 0;
};

// A Poller is a set of file descriptors that a higher level item is
// interested in, and a method to poll on them. For example:
//    - a server will typically keep a poller containing all connected channels,
//      so that it can find new calls to service
//    - a completion queue might keep a poller with an entry for each transport
//      that is servicing a call that it's tracking
class Poller : public PollableCollection {
 public:
  virtual ~Poller() {}

  class Worker {};

  // Do some work on a pollset.
  // May involve invoking asynchronous callbacks, or actually polling file
  // descriptors.
  //
  // Requires pollset's mutex locked. May unlock its mutex during its execution.
  //
  // worker is a (platform-specific) handle that can be used to wake up
  // from grpc_pollset_work before any events are received and before the
  // timeout has expired. It is both initialized and destroyed by
  // grpc_pollset_work. Initialization of worker is guaranteed to occur BEFORE
  // the pollset's mutex is released for the first time by grpc_pollset_work and
  // it is guaranteed that it will not be released by grpc_pollset_work AFTER
  // worker has been destroyed.
  //
  // It's legal for worker to be NULL: in that case, this specific thread can
  // not be directly woken with a kick, but maybe be indirectly (with a kick
  // against the pollset as a whole).
  //
  // Tries not to block past deadline. May call grpc_closure_list_run on
  // grpc_closure_list, without holding the pollset lock
  virtual grpc_error* Work(grpc_exec_ctx* exec_ctx, Worker** worker,
                           grpc_millis deadline) GRPC_MUST_USE_RESULT = 0;

  // Break one polling thread out of polling work for this pollset.
  // If specific_worker is non-NULL, then kick that worker.
  virtual grpc_error* Kick(grpc_exec_ctx* exec_ctx,
                           Worker* specific_worker) = 0;

  // Begin shutting down the pollset, and call on_done when done.
  // pollset's mutex must be held
  virtual void Shutdown(grpc_exec_ctx* exec_ctx, grpc_closure* on_done) = 0;

  // CHANGING: return the size of a Poller with the current polling engine
  // (will be moved to the polling engine interface directly in a future
  // revision)
  static size_t PollerSize();

  // CHANGING: instantiate a Poller with the current polling engine
  static Poller* Create(void* memory, gpr_mu** mu);

 protected:
  Poller() {}

 private:
  void AddToPollingJoin(PollingJoin* polling_join) override final;
  void RemoveFromPollingJoin(PollingJoin* polling_join) override final;
};

// A PollingJoin serves to join multiple pollers and pollables.
// Each Pollable that's added to a PollingJoin is added to ALL Poller instances
// contained within it
// Each Poller that's added to a PollingJoin receives ALL Pollable's that have
// been added to it
// (both in perpetuity)
//
// Using a PollingJoin is often cheaper than manually updating the same using
// just the Poller/Pollable interfaces as some polling engines have short-cut
// paths to bulk update pollers/pollables
class PollingJoin : public PollableCollection {
 public:
  // inherits AddPollable

  // remove a pollable from a join - it's unspecified whether this stops
  // existing Poller's in this join from polling on the Pollable, but guarantees
  // it will not be added to future Pollables
  virtual void RemovePollable(Pollable* pollable) = 0;

  // Add a Poller to the join
  virtual void AddPoller(Poller* poller) = 0;
  // and remove it again (so it doesn't receive new Pollables)
  virtual void RemovePoller(Poller* poller) = 0;

  // Merge a PollingJoin with this one
  // Each PollingJoin subsequently acts as a different handle to the same
  // join
  virtual void MergePollingJoin(PollingJoin* other_polling_join) = 0;

  // Helper to add a PollableCollection (by delegating to Poller/PollingJoin)
  void AddPollableCollection(PollableCollection* pollable_collection) {
    pollable_collection->AddToPollingJoin(this);
  }

 private:
  void AddToPollingJoin(PollingJoin* other_polling_join) override final;
  void RemoveFromPollingJoin(PollingJoin* polling_join) override final;
};

inline void Poller::AddToPollingJoin(PollingJoin* polling_join) {
  polling_join->AddPoller(this);
}

inline void PollingJoin::AddToPollingJoin(PollingJoin* other_polling_join) {
  other_polling_join->MergePollingJoin(this);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_POLLER_H */
