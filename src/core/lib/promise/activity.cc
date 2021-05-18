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

#include "src/core/lib/promise/activity.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

thread_local Activity* Activity::g_current_activity_ = nullptr;

///////////////////////////////////////////////////////////////////////////////
// HELPER TYPES

// Weak handle to an Activity.
// Handle can persist while Activity goes away.
class Activity::Handle {
 public:
  explicit Handle(Activity* activity) : activity_(activity) {}

  // Ref the Handle (not the activity).
  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }
  // Unref the Handle (not the activity).
  void Unref() {
    if (1 == refs_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  // Activity is going away... drop its reference and sever the connection back.
  void DropActivity() {
    mu_.Lock();
    assert(activity_ != nullptr);
    activity_ = nullptr;
    mu_.Unlock();
    Unref();
  }

  // Activity needs to wake up (if it still exists!) - wake it up, and drop the
  // ref that was kept for this handle.
  void WakeupAndUnref() LOCKS_EXCLUDED(mu_, activity_->mu_) {
    mu_.Lock();
    // Note that activity refcount can drop to zero, but we could win the lock
    // against DropActivity, so we need to only increase activities refcount if
    // it is non-zero.
    if (activity_ && activity_->RefIfNonZero()) {
      Activity* activity = activity_;
      mu_.Unlock();
      // Activity still exists and we have a reference: wake it up, and then
      // drop the ref.
      activity->Wakeup();
      activity->Unref();
    } else {
      // Could not get the activity - it's either gone or going. No need to wake
      // it up!
      mu_.Unlock();
    }
    // Drop the ref to the handle (we have one ref = one wakeup semantics).
    Unref();
  }

 private:
  // Two initial refs: one for the waiter that caused instantiation, one for the
  // activity.
  std::atomic<size_t> refs_{2};
  absl::Mutex mu_ ACQUIRED_AFTER(activity_->mu_);
  Activity* activity_ GUARDED_BY(mu_);
};

// Set the current activity at construction, clean it up at destruction.
class Activity::ScopedActivity {
 public:
  ScopedActivity(Activity* activity) {
    assert(g_current_activity_ == nullptr);
    g_current_activity_ = activity;
  }
  ~ScopedActivity() { g_current_activity_ = nullptr; }
  ScopedActivity(const ScopedActivity&) = delete;
  ScopedActivity& operator=(const ScopedActivity&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// ACTIVITY IMPLEMENTATION

Activity::Activity(CallbackScheduler* scheduler)
    : callback_scheduler_(scheduler) {}

Activity::~Activity() {
  if (handle_) {
    handle_->DropActivity();
  }
}

void Activity::Start(std::function<void(absl::Status)> on_done) {
  assert(!on_done_);
  on_done_ = std::move(on_done);
  Wakeup();
}

bool Activity::RefIfNonZero() {
  auto value = refs_.load(std::memory_order_acquire);
  do {
    if (value == 0) {
      return false;
    }
  } while (!refs_.compare_exchange_weak(
      value, value + 1, std::memory_order_release, std::memory_order_relaxed));
  return true;
}

Activity::Handle* Activity::RefHandle() {
  if (handle_ == nullptr) {
    // No handle created yet - construct it and return it.
    handle_ = new Handle(this);
    return handle_;
  } else {
    // Already had to create a handle, ref & return it.
    handle_->Ref();
    return handle_;
  }
}

void Activity::Wakeup() {
  // If there's no active activity, we can just run inline.
  if (g_current_activity_ == nullptr) {
    Step();
    return;
  }
  // If there is an active activity, but hey it's us, flag that and we'll loop
  // in RunLoop (that's calling from above here!).
  if (g_current_activity_ == this) {
    mu_.AssertHeld();
    got_wakeup_during_run_ = true;
    return;
  }
  // Can't safely run, so ask to run later.
  callback_scheduler_->Schedule([this]() { this->Step(); });
}

void Activity::Step() {
  Poll<absl::Status> result = PENDING;
  {
    // Poll the promise until things settle out under a lock.
    absl::MutexLock lock(&mu_);
    result = RunLoop();
  }
  // If we completed, call on_done.
  // No need to lock here since we guarantee this edge only occurs once.
  if (auto* status = result.get_ready()) {
    on_done_(std::move(*status));
    on_done_ = std::function<void(absl::Status)>();
  }
}

Poll<absl::Status> Activity::RunLoop() {
  if (done_) {
    // We might get some spurious wakeups after finishing.
    return PENDING;
  }
  // Set g_current_activity_ until we return.
  ScopedActivity scoped_activity(this);
  do {
    // Clear continuation flag - this might get set if Wakeup is called
    // down-stack.
    got_wakeup_during_run_ = false;
    // Run the promise.
    Poll<absl::Status> r = Run();
    if (auto* status = r.get_ready()) {
      // If complete, destroy the promise, flag done, and exit this loop.
      Stop();
      done_ = true;
      return std::move(*status);
    }
    // Continue looping til no wakeups occur.
  } while (got_wakeup_during_run_);
  return PENDING;
}

void Activity::Cancel() {
  bool was_done;
  {
    absl::MutexLock lock(&mu_);
    // Drop the promise if it exists.
    Stop();
    // Check if we were done, and flag done.
    was_done = done_;
    done_ = true;
  }
  // If we were not done, then call the on_done callback.
  if (!was_done) {
    on_done_(absl::CancelledError());
  }
}

///////////////////////////////////////////////////////////////////////////////
// WAITSET IMPLEMENTATION

WaitSet::WaitSet() = default;

WaitSet::~WaitSet() {
  for (auto* h : pending_) {
    h->WakeupAndUnref();
  }
}

Pending WaitSet::pending() {
  Activity::g_current_activity_->mu_.AssertHeld();
  // Get a reffed handle to the current activity.
  Activity::Handle* h = Activity::g_current_activity_->RefHandle();
  // Insert it into our pending list.
  if (!pending_.insert(h).second) {
    // If it was already there, we can drop it immediately.
    h->Unref();
  }
  return PENDING;
}

void WaitSet::WakeAllAndUnlock() {
  // Take the entire pending set.
  decltype(pending_) prior;
  prior.swap(pending_);
  // Unlock so we're not holding a mutex during wakeups
  mu_.Unlock();
  // Wakeup all the waiting activities!
  for (auto* h : prior) {
    h->WakeupAndUnref();
  }
}

///////////////////////////////////////////////////////////////////////////////
// SINGLEWAITER IMPLEMENTATION

SingleWaiter::SingleWaiter() : waiter_(nullptr) {}

SingleWaiter::~SingleWaiter() { Wakeup(waiter_); }

Pending SingleWaiter::pending() {
  assert(Activity::g_current_activity_);
  Activity::g_current_activity_->mu_.AssertHeld();
  if (waiter_ == Activity::g_current_activity_) {
    return PENDING;
  }
  if (waiter_ != nullptr) {
    waiter_->Unref();
  }
  waiter_ = Activity::g_current_activity_;
  waiter_->Ref();
  return PENDING;
}

void SingleWaiter::WakeAndUnlock() {
  Activity* activity = waiter_;
  waiter_ = nullptr;
  mu_.Unlock();
  Wakeup(activity);
}

void SingleWaiter::Wakeup(Activity* activity) {
  if (activity == nullptr) {
    return;
  }
  activity->Wakeup();
  activity->Unref();
}

}  // namespace grpc_core
