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

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/activity.h"

#include <stddef.h>

#include "src/core/lib/gprpp/atomic_utils.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

GPR_THREAD_LOCAL(Activity*) Activity::g_current_activity_{nullptr};

namespace promise_detail {

///////////////////////////////////////////////////////////////////////////////
// HELPER TYPES

// Weak handle to an Activity.
// Handle can persist while Activity goes away.
class FreestandingActivity::Handle final : public Wakeable {
 public:
  explicit Handle(FreestandingActivity* activity) : activity_(activity) {}

  // Ref the Handle (not the activity).
  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }

  // Activity is going away... drop its reference and sever the connection back.
  void DropActivity() ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    GPR_ASSERT(activity_ != nullptr);
    activity_ = nullptr;
    mu_.Unlock();
    Unref();
  }

  // Activity needs to wake up (if it still exists!) - wake it up, and drop the
  // ref that was kept for this handle.
  void Wakeup() override ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    // Note that activity refcount can drop to zero, but we could win the lock
    // against DropActivity, so we need to only increase activities refcount if
    // it is non-zero.
    if (activity_ && activity_->RefIfNonzero()) {
      FreestandingActivity* activity = activity_;
      mu_.Unlock();
      // Activity still exists and we have a reference: wake it up, which will
      // drop the ref.
      activity->Wakeup();
    } else {
      // Could not get the activity - it's either gone or going. No need to wake
      // it up!
      mu_.Unlock();
    }
    // Drop the ref to the handle (we have one ref = one wakeup semantics).
    Unref();
  }

  void Drop() override { Unref(); }

 private:
  // Unref the Handle (not the activity).
  void Unref() {
    if (1 == refs_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  // Two initial refs: one for the waiter that caused instantiation, one for the
  // activity.
  std::atomic<size_t> refs_{2};
  Mutex mu_ ABSL_ACQUIRED_AFTER(activity_->mu_);
  FreestandingActivity* activity_ ABSL_GUARDED_BY(mu_);
};

///////////////////////////////////////////////////////////////////////////////
// ACTIVITY IMPLEMENTATION

bool FreestandingActivity::RefIfNonzero() { return IncrementIfNonzero(&refs_); }

FreestandingActivity::Handle* FreestandingActivity::RefHandle() {
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

void FreestandingActivity::DropHandle() {
  handle_->DropActivity();
  handle_ = nullptr;
}

Waker FreestandingActivity::MakeNonOwningWaker() {
  mu_.AssertHeld();
  return Waker(RefHandle());
}

}  // namespace promise_detail
}  // namespace grpc_core
