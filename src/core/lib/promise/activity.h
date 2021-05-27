// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_PROMISE_ACTIVITY_H
#define GRPC_CORE_LIB_PROMISE_ACTIVITY_H

#include <functional>
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Helper to schedule callbacks for activities.
class CallbackScheduler {
 public:
  // Schedule a callback. callback should be called outside of any Activity
  // system mutexes.
  virtual void Schedule(std::function<void()> callback) = 0;
};

class WaitSet;
class SingleWaiter;
class IntraActivityWaiter;

// An Activity tracks execution of a single promise.
// It executes the promise under a mutex.
// When the promise stalls, it registers the containing activity to be woken up
// later.
// The activity takes a callback, which will be called exactly once with the
// result of execution.
// Activity execution may be cancelled by simply deleting the activity. In such
// a case, if execution had not already finished, the done callback would be
// called with absl::CancelledError().
// Activity also takes a CallbackScheduler instance on which to schedule
// callbacks to itself in a lock-clean environment.
class Activity {
 public:
  // Cancel execution of the underlying promise.
  void Cancel() LOCKS_EXCLUDED(mu_);

  // Destroy the Activity - used for the type alias ActivityPtr.
  struct Deleter {
    void operator()(Activity* activity) {
      activity->Cancel();
      activity->Unref();
    }
  };

  virtual size_t Size() = 0;

 protected:
  explicit Activity(CallbackScheduler* scheduler);
  virtual ~Activity();

  // Start execution of the underlying promise - effectively finalizes
  // initialization.
  void Start(std::function<void(absl::Status)> on_done);
  absl::Mutex* mu() LOCK_RETURNED(mu_) { return &mu_; }

 private:
  friend class WaitSet;
  friend class SingleWaiter;
  friend class IntraActivityWaiter;
  class ScopedActivity;
  class Handle;

  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (1 == refs_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  // Arrange to wake the activity and poll the underlying promise once again.
  void Wakeup();
  // In response to Wakeup, run the Promise state machine again until it
  // settles. Then check for completion, and if we have completed, call on_done.
  void Step() LOCKS_EXCLUDED(mu_);
  // Run an activity until it's either finished, or pending without waking
  // itself up.
  Poll<absl::Status> RunLoop() EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Poll the promise once.
  virtual Poll<absl::Status> Run() EXCLUSIVE_LOCKS_REQUIRED(mu_) = 0;
  // Notification that we're no longer executing - it's ok to destruct the
  // promise.
  virtual void Stop() EXCLUSIVE_LOCKS_REQUIRED(mu_) = 0;
  // Return a Handle instance with a ref so that it can be stored waiting for
  // some wakeup.
  Handle* RefHandle() EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // If our refcount is non-zero, ref and return true.
  // Otherwise, return false.
  bool RefIfNonZero();

  // Current refcount.
  std::atomic<uint64_t> refs_{1};
  // Scheduler of callbacks.
  CallbackScheduler* const callback_scheduler_;
  // Callback on completion of the promise.
  std::function<void(absl::Status)> on_done_;
  // All promise execution occurs under this mutex.
  absl::Mutex mu_;
  // Handle for long waits. Allows a very small weak pointer type object to
  // queue for wakeups while Activity may be deleted earlier.
  Handle* handle_ GUARDED_BY(mu_) = nullptr;
  // If wakeup is called during Promise polling, we raise this flag and repoll
  // until things settle out.
  bool got_wakeup_during_run_ GUARDED_BY(mu_) = false;
  // Has execution completed?
  bool done_ GUARDED_BY(mu_) = false;
  // Set during RunLoop to the Activity that's executing.
  // Being set implies that mu_ is held.
  static thread_local Activity* g_current_activity_;
};

// Owned pointer to one Activity.
using ActivityPtr = std::unique_ptr<Activity, Activity::Deleter>;

namespace activity_detail {

template <typename Context>
class ContextHolder {
 public:
  explicit ContextHolder(Context value) : value_(std::move(value)) {}
  Context* GetContext() { return &value_; }

 private:
  Context value_;
};

template <typename Context>
class ContextHolder<Context*> {
 public:
  explicit ContextHolder(Context* value) : value_(value) {}
  Context* GetContext() { return value_; }

 private:
  Context* value_;
};

template <typename... Contexts>
class EnterContexts : public context_detail::Context<Contexts>... {
 public:
  explicit EnterContexts(Contexts*... contexts)
      : context_detail::Context<Contexts>(contexts)... {}
};

// Implementation details for an Activity of an arbitrary type of promise.
template <class Factory, typename... Contexts>
class PromiseActivity final
    : public Activity,
      private activity_detail::ContextHolder<Contexts>... {
 public:
  PromiseActivity(Factory promise_factory,
                  std::function<void(absl::Status)> on_done,
                  CallbackScheduler* callback_scheduler, Contexts... contexts)
      : Activity(callback_scheduler),
        ContextHolder<Contexts>(std::move(contexts))...,
        state_(std::move(promise_factory)) {
    Start(std::move(on_done));
  }

  size_t Size() override { return sizeof(*this); }

 private:
  Poll<absl::Status> Run() final EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    EnterContexts<Contexts...> contexts(
        static_cast<ContextHolder<Contexts>*>(this)->GetContext()...);
    return absl::visit(RunState{this}, state_);
  }

  void Stop() final EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    state_.template emplace<Stopped>();
  }

 private:
  using Promise = decltype(std::declval<Factory>()());
  struct Stopped {};
  // We can be in one of three states:
  // - Factory => we've never been polled. We delay Promise construction until
  //              late in construction so that we can schedule wakeups against
  //              the activity during construction.
  // - Promise => we've started, and are polling for completion.
  // - Stopped => execution has completed. A state by itself to ensure we can
  //              run destructors on Promise.
  absl::variant<Factory, Promise, Stopped> state_ GUARDED_BY(mu());

  // Functor to advance the state machine.
  struct RunState {
    PromiseActivity* self;

    Poll<absl::Status> operator()(Factory& factory) {
      self->mu()->AssertHeld();
      // Construct the promise from the initial promise factory.
      auto promise = factory();
      // Poll the promise once.
      auto r = promise();
      if (r.pending()) {
        // If it doesn't complete immediately, we need to store state for next
        // time.
        self->state_.template emplace<Promise>(std::move(promise));
      }
      return r;
    }

    Poll<absl::Status> operator()(Promise& promise) {
      self->mu()->AssertHeld();
      // Normal execution: just poll.
      return promise();
    }

    // Execution has been stopped - ought not be reachable.
    Poll<absl::Status> operator()(Stopped& stopped) { abort(); }
  };
};

}  // namespace activity_detail

// Given a functor that returns a promise (a promise factory), a callback for
// completion, and a callback scheduler, construct an activity.
template <typename Factory, typename... Contexts>
ActivityPtr MakeActivity(Factory promise_factory,
                         std::function<void(absl::Status)> on_done,
                         CallbackScheduler* callback_scheduler,
                         Contexts... contexts) {
  return ActivityPtr(new activity_detail::PromiseActivity<Factory, Contexts...>(
      std::move(promise_factory), std::move(on_done), callback_scheduler,
      std::move(contexts)...));
}

// Helper type that can be used to enqueue many Activities waiting for some
// external state.
// Typically the external state should be guarded by mu(), and a call to
// WakeAllAndUnlock should be made when the state changes.
// Promises should bottom out polling inside pending(), which will register for
// wakeup and return kPending.
// Queues handles to Activities, and not Activities themselves, meaning that if
// an Activity is destroyed prior to wakeup we end up holding only a small
// amount of memory (around 16 bytes + malloc overhead) until the next wakeup
// occurs.
class WaitSet final {
 public:
  WaitSet();
  ~WaitSet();
  WaitSet(const WaitSet&) = delete;
  WaitSet& operator=(const WaitSet&) = delete;
  // Register for wakeup, return kPending. If state is not ready to proceed,
  // Promises should bottom out here.
  Pending pending() EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Wake all activities that have called pending() since the last
  // WakeAllAndUnlock().
  void WakeAllAndUnlock() UNLOCK_FUNCTION(mu_);

  absl::Mutex* mu() LOCK_RETURNED(mu_) { return &mu_; }

 private:
  absl::Mutex mu_;
  // Handles to activities that need to be awoken.
  absl::flat_hash_set<Activity::Handle*> pending_ GUARDED_BY(mu_);
};

// Helper type to enqueue at most one Activity waiting for some external state.
// Keeps the entire Activity reffed until wakeup or destruction.
class SingleWaiter final {
 public:
  SingleWaiter();
  ~SingleWaiter();
  SingleWaiter(const SingleWaiter&) = delete;
  SingleWaiter& operator=(const SingleWaiter&) = delete;
  // Register for wakeup, return kPending. If state is not ready to proceed,
  // Promises should bottom out here.
  Pending pending() EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Wake the activity that called pending() after the last WakeAllAndUnlock().
  void WakeAndUnlock() UNLOCK_FUNCTION(mu_);

  absl::Mutex* mu() LOCK_RETURNED(mu_) { return &mu_; }

 private:
  absl::Mutex mu_;
  Activity* waiter_ GUARDED_BY(mu_) = nullptr;

  void Wakeup(Activity* activity);
};

// Helper type to track wakeups between objects in the same activity.
// Can be fairly fast as no ref counting or locking needs to occur.
class IntraActivityWaiter {
 public:
  // Register for wakeup, return kPending. If state is not ready to proceed,
  // Promises should bottom out here.
  Pending pending() {
    waiting_ = true;
    return kPending;
  }
  // Wake the activity
  void Wake() {
    if (waiting_) {
      waiting_ = false;
      Activity::g_current_activity_->mu_.AssertHeld();
      Activity::g_current_activity_->got_wakeup_during_run_ = true;
    }
  }

 private:
  bool waiting_ = false;
};

}  // namespace grpc_core

#endif