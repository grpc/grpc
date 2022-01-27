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

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"

#include <grpc/support/log.h>

#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// A Wakeable object is used by queues to wake activities.
class Wakeable {
 public:
  // Wake up the underlying activity.
  // After calling, this Wakeable cannot be used again.
  virtual void Wakeup() = 0;
  // Drop this wakeable without waking up the underlying activity.
  virtual void Drop() = 0;

 protected:
  inline ~Wakeable() {}
};

// An owning reference to a Wakeable.
// This type is non-copyable but movable.
class Waker {
 public:
  explicit Waker(Wakeable* wakeable) : wakeable_(wakeable) {}
  Waker() : wakeable_(&unwakeable_) {}
  ~Waker() { wakeable_->Drop(); }
  Waker(const Waker&) = delete;
  Waker& operator=(const Waker&) = delete;
  Waker(Waker&& other) noexcept : wakeable_(other.wakeable_) {
    other.wakeable_ = &unwakeable_;
  }
  Waker& operator=(Waker&& other) noexcept {
    std::swap(wakeable_, other.wakeable_);
    return *this;
  }

  // Wake the underlying activity.
  void Wakeup() {
    wakeable_->Wakeup();
    wakeable_ = &unwakeable_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const Waker& w) {
    return H::combine(std::move(h), w.wakeable_);
  }

  bool operator==(const Waker& other) const noexcept {
    return wakeable_ == other.wakeable_;
  }

 private:
  class Unwakeable final : public Wakeable {
   public:
    void Wakeup() final {}
    void Drop() final {}
  };

  Wakeable* wakeable_;
  static Unwakeable unwakeable_;
};

// An Activity tracks execution of a single promise.
// It executes the promise under a mutex.
// When the promise stalls, it registers the containing activity to be woken up
// later.
// The activity takes a callback, which will be called exactly once with the
// result of execution.
// Activity execution may be cancelled by simply deleting the activity. In such
// a case, if execution had not already finished, the done callback would be
// called with absl::CancelledError().
class Activity : private Wakeable {
 public:
  // Cancel execution of the underlying promise.
  virtual void Cancel() ABSL_LOCKS_EXCLUDED(mu_) = 0;

  // Destroy the Activity - used for the type alias ActivityPtr.
  struct Deleter {
    void operator()(Activity* activity) {
      activity->Cancel();
      activity->Unref();
    }
  };

  // Fetch the size of the implementation of this activity.
  virtual size_t Size() = 0;

  // Force wakeup from the outside.
  // This should be rarely needed, and usages should be accompanied with a note
  // on why it's not possible to wakeup with a Waker object.
  // Nevertheless, it's sometimes useful for integrations with Activity to force
  // an Activity to repoll.
  void ForceWakeup() { MakeOwningWaker().Wakeup(); }

  // Wakeup the current threads activity - will force a subsequent poll after
  // the one that's running.
  static void WakeupCurrent() {
    current()->SetActionDuringRun(ActionDuringRun::kWakeup);
  }

  // Return the current activity.
  // Additionally:
  // - assert that there is a current activity (and catch bugs if there's not)
  // - indicate to thread safety analysis that the current activity is indeed
  //   locked
  // - back up that assertation with a runtime check in debug builds (it's
  //   prohibitively expensive in non-debug builds)
  static Activity* current() ABSL_ASSERT_EXCLUSIVE_LOCK(current()->mu_) {
#ifndef NDEBUG
    GPR_ASSERT(g_current_activity_);
    if (g_current_activity_ != nullptr) {
      g_current_activity_->mu_.AssertHeld();
    }
#endif
    return g_current_activity_;
  }

  // Produce an activity-owning Waker. The produced waker will keep the activity
  // alive until it's awoken or dropped.
  Waker MakeOwningWaker() {
    Ref();
    return Waker(this);
  }

  // Produce a non-owning Waker. The waker will own a small heap allocated weak
  // pointer to this activity. This is more suitable for wakeups that may not be
  // delivered until long after the activity should be destroyed.
  Waker MakeNonOwningWaker() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 protected:
  // Action received during a run, in priority order.
  // If more than one action is received during a run, we use max() to resolve
  // which one to report (so Cancel overrides Wakeup).
  enum class ActionDuringRun : uint8_t {
    kNone,    // No action occured during run.
    kWakeup,  // A wakeup occured during run.
    kCancel,  // Cancel was called during run.
  };

  inline virtual ~Activity() {
    if (handle_) {
      DropHandle();
    }
  }

  // All promise execution occurs under this mutex.
  Mutex mu_;

  // Check if this activity is the current activity executing on the current
  // thread.
  bool is_current() const { return this == g_current_activity_; }
  // Check if there is an activity executing on the current thread.
  static bool have_current() { return g_current_activity_ != nullptr; }
  // Check if we got an internal wakeup since the last time this function was
  // called.
  ActionDuringRun GotActionDuringRun() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return absl::exchange(action_during_run_, ActionDuringRun::kNone);
  }

  // Set the current activity at construction, clean it up at destruction.
  class ScopedActivity {
   public:
    explicit ScopedActivity(Activity* activity) {
      GPR_ASSERT(g_current_activity_ == nullptr);
      g_current_activity_ = activity;
    }
    ~ScopedActivity() { g_current_activity_ = nullptr; }
    ScopedActivity(const ScopedActivity&) = delete;
    ScopedActivity& operator=(const ScopedActivity&) = delete;
  };

  // Implementors of Wakeable::Wakeup should call this after the wakeup has
  // completed.
  void WakeupComplete() { Unref(); }

  // Mark the current activity as being cancelled (so we can actually cancel it
  // after polling).
  void CancelCurrent() {
    current()->SetActionDuringRun(ActionDuringRun::kCancel);
  }

 private:
  class Handle;

  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (1 == refs_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  // Return a Handle instance with a ref so that it can be stored waiting for
  // some wakeup.
  Handle* RefHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // If our refcount is non-zero, ref and return true.
  // Otherwise, return false.
  bool RefIfNonzero();
  // Drop the (proved existing) wait handle.
  void DropHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Set the action that occured during this run.
  // We use max to combine actions so that cancellation overrides wakeups.
  void SetActionDuringRun(ActionDuringRun action)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    action_during_run_ = std::max(action_during_run_, action);
  }

  // Current refcount.
  std::atomic<uint32_t> refs_{1};
  // If wakeup is called during Promise polling, we set this to Wakeup and
  // repoll. If cancel is called during Promise polling, we set this to Cancel
  // and cancel at the end of polling.
  ActionDuringRun action_during_run_ ABSL_GUARDED_BY(mu_) =
      ActionDuringRun::kNone;
  // Handle for long waits. Allows a very small weak pointer type object to
  // queue for wakeups while Activity may be deleted earlier.
  Handle* handle_ ABSL_GUARDED_BY(mu_) = nullptr;
  // Set during RunLoop to the Activity that's executing.
  // Being set implies that mu_ is held.
  static GPR_THREAD_LOCAL(Activity*) g_current_activity_;
};

// Owned pointer to one Activity.
using ActivityPtr = std::unique_ptr<Activity, Activity::Deleter>;

namespace promise_detail {

template <typename Context>
class ContextHolder {
 public:
  using ContextType = Context;

  explicit ContextHolder(Context value) : value_(std::move(value)) {}
  Context* GetContext() { return &value_; }

 private:
  Context value_;
};

template <typename Context>
class ContextHolder<Context*> {
 public:
  using ContextType = Context;

  explicit ContextHolder(Context* value) : value_(value) {}
  Context* GetContext() { return value_; }

 private:
  Context* value_;
};

template <typename Context, typename Deleter>
class ContextHolder<std::unique_ptr<Context, Deleter>> {
 public:
  using ContextType = Context;

  explicit ContextHolder(std::unique_ptr<Context, Deleter> value)
      : value_(std::move(value)) {}
  Context* GetContext() { return value_.get(); }

 private:
  std::unique_ptr<Context, Deleter> value_;
};

template <typename HeldContext>
using ContextTypeFromHeld = typename ContextHolder<HeldContext>::ContextType;

template <typename... Contexts>
class ActivityContexts : public ContextHolder<Contexts>... {
 public:
  explicit ActivityContexts(Contexts&&... contexts)
      : ContextHolder<Contexts>(std::forward<Contexts>(contexts))... {}

  class ScopedContext : public Context<ContextTypeFromHeld<Contexts>>... {
   public:
    explicit ScopedContext(ActivityContexts* contexts)
        : Context<ContextTypeFromHeld<Contexts>>(
              static_cast<ContextHolder<Contexts>*>(contexts)
                  ->GetContext())... {}
  };
};

// Implementation details for an Activity of an arbitrary type of promise.
// There should exist a static function:
// struct WakeupScheduler {
//   template <typename ActivityType>
//   void ScheduleWakeup(ActivityType* activity);
// };
// This function should arrange that activity->RunScheduledWakeup() be invoked
// at the earliest opportunity.
// It can assume that activity will remain live until RunScheduledWakeup() is
// invoked, and that a given activity will not be concurrently scheduled again
// until its RunScheduledWakeup() has been invoked.
template <class F, class WakeupScheduler, class OnDone, typename... Contexts>
class PromiseActivity final : public Activity,
                              private ActivityContexts<Contexts...> {
 public:
  using Factory = PromiseFactory<void, F>;
  PromiseActivity(F promise_factory, WakeupScheduler wakeup_scheduler,
                  OnDone on_done, Contexts&&... contexts)
      : Activity(),
        ActivityContexts<Contexts...>(std::forward<Contexts>(contexts)...),
        wakeup_scheduler_(std::move(wakeup_scheduler)),
        on_done_(std::move(on_done)) {
    // Lock, construct an initial promise from the factory, and step it.
    // This may hit a waiter, which could expose our this pointer to other
    // threads, meaning we do need to hold this mutex even though we're still
    // constructing.
    mu_.Lock();
    auto status = Start(Factory(std::move(promise_factory)));
    mu_.Unlock();
    // We may complete immediately.
    if (status.has_value()) {
      on_done_(std::move(*status));
    }
  }

  ~PromiseActivity() override {
    // We shouldn't destruct without calling Cancel() first, and that must get
    // us to be done_, so we assume that and have no logic to destruct the
    // promise here.
    GPR_ASSERT(done_);
  }

  size_t Size() override { return sizeof(*this); }

  void Cancel() final {
    if (Activity::is_current()) {
      CancelCurrent();
      return;
    }
    bool was_done;
    {
      MutexLock lock(&mu_);
      // Check if we were done, and flag done.
      was_done = done_;
      if (!done_) MarkDone();
    }
    // If we were not done, then call the on_done callback.
    if (!was_done) {
      on_done_(absl::CancelledError());
    }
  }

  void RunScheduledWakeup() {
    GPR_ASSERT(wakeup_scheduled_.exchange(false, std::memory_order_acq_rel));
    Step();
    WakeupComplete();
  }

 private:
  using typename ActivityContexts<Contexts...>::ScopedContext;

  // Wakeup this activity. Arrange to poll the activity again at a convenient
  // time: this could be inline if it's deemed safe, or it could be by passing
  // the activity to an external threadpool to run. If the activity is already
  // running on this thread, a note is taken of such and the activity is
  // repolled if it doesn't complete.
  void Wakeup() final {
    // If there is an active activity, but hey it's us, flag that and we'll loop
    // in RunLoop (that's calling from above here!).
    if (Activity::is_current()) {
      WakeupCurrent();
      WakeupComplete();
      return;
    }
    if (!wakeup_scheduled_.exchange(true, std::memory_order_acq_rel)) {
      // Can't safely run, so ask to run later.
      wakeup_scheduler_.ScheduleWakeup(this);
    } else {
      // Already a wakeup scheduled for later, drop ref.
      WakeupComplete();
    }
  }

  // Drop a wakeup
  void Drop() final { this->WakeupComplete(); }

  // Notification that we're no longer executing - it's ok to destruct the
  // promise.
  void MarkDone() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    GPR_ASSERT(!done_);
    done_ = true;
    Destruct(&promise_holder_.promise);
  }

  // In response to Wakeup, run the Promise state machine again until it
  // settles. Then check for completion, and if we have completed, call on_done.
  void Step() ABSL_LOCKS_EXCLUDED(mu_) {
    // Poll the promise until things settle out under a lock.
    mu_.Lock();
    if (done_) {
      // We might get some spurious wakeups after finishing.
      mu_.Unlock();
      return;
    }
    auto status = RunStep();
    mu_.Unlock();
    if (status.has_value()) {
      on_done_(std::move(*status));
    }
  }

  // The main body of a step: set the current activity, and any contexts, and
  // then run the main polling loop. Contained in a function by itself in
  // order to keep the scoping rules a little easier in Step().
  absl::optional<absl::Status> RunStep() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    ScopedActivity scoped_activity(this);
    ScopedContext contexts(this);
    return StepLoop();
  }

  // Similarly to RunStep, but additionally construct the promise from a
  // promise factory before entering the main loop. Called once from the
  // constructor.
  absl::optional<absl::Status> Start(Factory promise_factory)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    ScopedActivity scoped_activity(this);
    ScopedContext contexts(this);
    Construct(&promise_holder_.promise, promise_factory.Once());
    return StepLoop();
  }

  // Until there are no wakeups from within and the promise is incomplete:
  // poll the promise.
  absl::optional<absl::Status> StepLoop() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    GPR_ASSERT(is_current());
    while (true) {
      // Run the promise.
      GPR_ASSERT(!done_);
      auto r = promise_holder_.promise();
      if (auto* status = absl::get_if<kPollReadyIdx>(&r)) {
        // If complete, destroy the promise, flag done, and exit this loop.
        MarkDone();
        return IntoStatus(status);
      }
      // Continue looping til no wakeups occur.
      switch (GotActionDuringRun()) {
        case ActionDuringRun::kNone:
          return {};
        case ActionDuringRun::kWakeup:
          break;
        case ActionDuringRun::kCancel:
          MarkDone();
          return absl::CancelledError();
      }
    }
  }

  using Promise = typename Factory::Promise;
  // Scheduler for wakeups
  GPR_NO_UNIQUE_ADDRESS WakeupScheduler wakeup_scheduler_;
  // Callback on completion of the promise.
  GPR_NO_UNIQUE_ADDRESS OnDone on_done_;
  // Has execution completed?
  GPR_NO_UNIQUE_ADDRESS bool done_ ABSL_GUARDED_BY(mu_) = false;
  // Is there a wakeup scheduled?
  GPR_NO_UNIQUE_ADDRESS std::atomic<bool> wakeup_scheduled_{false};
  // We wrap the promise in a union to allow control over the construction
  // simultaneously with annotating mutex requirements and noting that the
  // promise contained may not use any memory.
  union PromiseHolder {
    PromiseHolder() {}
    ~PromiseHolder() {}
    GPR_NO_UNIQUE_ADDRESS Promise promise;
  };
  GPR_NO_UNIQUE_ADDRESS PromiseHolder promise_holder_ ABSL_GUARDED_BY(mu_);
};

}  // namespace promise_detail

// Given a functor that returns a promise (a promise factory), a callback for
// completion, and a callback scheduler, construct an activity.
template <typename Factory, typename WakeupScheduler, typename OnDone,
          typename... Contexts>
ActivityPtr MakeActivity(Factory promise_factory,
                         WakeupScheduler wakeup_scheduler, OnDone on_done,
                         Contexts&&... contexts) {
  return ActivityPtr(
      new promise_detail::PromiseActivity<Factory, WakeupScheduler, OnDone,
                                          Contexts...>(
          std::move(promise_factory), std::move(wakeup_scheduler),
          std::move(on_done), std::forward<Contexts>(contexts)...));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_ACTIVITY_H
