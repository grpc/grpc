/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <grpc/support/atm.h>

#include "src/core/lib/gpr/mpscq.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/dynamic_annotations.h"

// A simple, lock-free mechanism for serializing activity related to a
// single call.  This is similar to a combiner but is more lightweight.
//
// It requires the callback (or, in the common case where the callback
// actually kicks off a chain of callbacks, the last callback in that
// chain) to explicitly indicate (by calling GRPC_CALL_COMBINER_STOP())
// when it is done with the action that was kicked off by the original
// callback.

extern grpc_core::TraceFlag grpc_call_combiner_trace;

struct grpc_call_combiner {
  gpr_atm size = 0;  // size_t, num closures in queue or currently executing
  gpr_mpscq queue;
  // Either 0 (if not cancelled and no cancellation closure set),
  // a grpc_closure* (if the lowest bit is 0),
  // or a grpc_error* (if the lowest bit is 1).
  gpr_atm cancel_state = 0;
#ifdef GRPC_TSAN_ENABLED
  // A fake ref-counted lock that is kept alive after the destruction of
  // grpc_call_combiner, when we are running the original closure.
  //
  // Ideally we want to lock and unlock the call combiner as a pointer, when the
  // callback is called. However, original_closure is free to trigger
  // anything on the call combiner (including destruction of grpc_call).
  // Thus, we need a ref-counted structure that can outlive the call combiner.
  struct TsanLock
      : public grpc_core::RefCounted<TsanLock,
                                     grpc_core::NonPolymorphicRefCount> {
    TsanLock() { TSAN_ANNOTATE_RWLOCK_CREATE(&taken); }
    ~TsanLock() { TSAN_ANNOTATE_RWLOCK_DESTROY(&taken); }

    // To avoid double-locking by the same thread, we should acquire/release
    // the lock only when taken is false. On each acquire taken must be set to
    // true.
    std::atomic<bool> taken{false};
  };
  grpc_core::RefCountedPtr<TsanLock> tsan_lock =
      grpc_core::MakeRefCounted<TsanLock>();
  grpc_closure tsan_closure;
  grpc_closure* original_closure;
#endif
};

// Assumes memory was initialized to zero.
void grpc_call_combiner_init(grpc_call_combiner* call_combiner);

void grpc_call_combiner_destroy(grpc_call_combiner* call_combiner);

#ifndef NDEBUG
#define GRPC_CALL_COMBINER_START(call_combiner, closure, error, reason)   \
  grpc_call_combiner_start((call_combiner), (closure), (error), __FILE__, \
                           __LINE__, (reason))
#define GRPC_CALL_COMBINER_STOP(call_combiner, reason) \
  grpc_call_combiner_stop((call_combiner), __FILE__, __LINE__, (reason))
/// Starts processing \a closure on \a call_combiner.
void grpc_call_combiner_start(grpc_call_combiner* call_combiner,
                              grpc_closure* closure, grpc_error* error,
                              const char* file, int line, const char* reason);
/// Yields the call combiner to the next closure in the queue, if any.
void grpc_call_combiner_stop(grpc_call_combiner* call_combiner,
                             const char* file, int line, const char* reason);
#else
#define GRPC_CALL_COMBINER_START(call_combiner, closure, error, reason) \
  grpc_call_combiner_start((call_combiner), (closure), (error), (reason))
#define GRPC_CALL_COMBINER_STOP(call_combiner, reason) \
  grpc_call_combiner_stop((call_combiner), (reason))
/// Starts processing \a closure on \a call_combiner.
void grpc_call_combiner_start(grpc_call_combiner* call_combiner,
                              grpc_closure* closure, grpc_error* error,
                              const char* reason);
/// Yields the call combiner to the next closure in the queue, if any.
void grpc_call_combiner_stop(grpc_call_combiner* call_combiner,
                             const char* reason);
#endif

/// Registers \a closure to be invoked by \a call_combiner when
/// grpc_call_combiner_cancel() is called.
///
/// Once a closure is registered, it will always be scheduled exactly
/// once; this allows the closure to hold references that will be freed
/// regardless of whether or not the call was cancelled.  If a cancellation
/// does occur, the closure will be scheduled with the cancellation error;
/// otherwise, it will be scheduled with GRPC_ERROR_NONE.
///
/// The closure will be scheduled in the following cases:
/// - If grpc_call_combiner_cancel() was called prior to registering the
///   closure, it will be scheduled immediately with the cancelation error.
/// - If grpc_call_combiner_cancel() is called after registering the
///   closure, the closure will be scheduled with the cancellation error.
/// - If grpc_call_combiner_set_notify_on_cancel() is called again to
///   register a new cancellation closure, the previous cancellation
///   closure will be scheduled with GRPC_ERROR_NONE.
///
/// If \a closure is NULL, then no closure will be invoked on
/// cancellation; this effectively unregisters the previously set closure.
/// However, most filters will not need to explicitly unregister their
/// callbacks, as this is done automatically when the call is destroyed. Filters
/// that schedule the cancellation closure on ExecCtx do not need to take a ref
/// on the call stack to guarantee closure liveness. This is done by explicitly
/// flushing ExecCtx after the unregistration during call destruction.
void grpc_call_combiner_set_notify_on_cancel(grpc_call_combiner* call_combiner,
                                             grpc_closure* closure);

/// Indicates that the call has been cancelled.
void grpc_call_combiner_cancel(grpc_call_combiner* call_combiner,
                               grpc_error* error);

namespace grpc_core {

// Helper for running a list of closures in a call combiner.
//
// Each callback running in the call combiner will eventually be
// returned to the surface, at which point the surface will yield the
// call combiner.  So when we are running in the call combiner and have
// more than one callback to return to the surface, we need to re-enter
// the call combiner for all but one of those callbacks.
class CallCombinerClosureList {
 public:
  CallCombinerClosureList() {}

  // Adds a closure to the list.  The closure must eventually result in
  // the call combiner being yielded.
  void Add(grpc_closure* closure, grpc_error* error, const char* reason) {
    closures_.emplace_back(closure, error, reason);
  }

  // Runs all closures in the call combiner and yields the call combiner.
  //
  // All but one of the closures in the list will be scheduled via
  // GRPC_CALL_COMBINER_START(), and the remaining closure will be
  // scheduled via GRPC_CLOSURE_SCHED(), which will eventually result in
  // yielding the call combiner.  If the list is empty, then the call
  // combiner will be yielded immediately.
  void RunClosures(grpc_call_combiner* call_combiner) {
    if (closures_.empty()) {
      GRPC_CALL_COMBINER_STOP(call_combiner, "no closures to schedule");
      return;
    }
    for (size_t i = 1; i < closures_.size(); ++i) {
      auto& closure = closures_[i];
      GRPC_CALL_COMBINER_START(call_combiner, closure.closure, closure.error,
                               closure.reason);
    }
    if (grpc_call_combiner_trace.enabled()) {
      gpr_log(GPR_INFO,
              "CallCombinerClosureList executing closure while already "
              "holding call_combiner %p: closure=%p error=%s reason=%s",
              call_combiner, closures_[0].closure,
              grpc_error_string(closures_[0].error), closures_[0].reason);
    }
    // This will release the call combiner.
    GRPC_CLOSURE_SCHED(closures_[0].closure, closures_[0].error);
    closures_.clear();
  }

  // Runs all closures in the call combiner, but does NOT yield the call
  // combiner.  All closures will be scheduled via GRPC_CALL_COMBINER_START().
  void RunClosuresWithoutYielding(grpc_call_combiner* call_combiner) {
    for (size_t i = 0; i < closures_.size(); ++i) {
      auto& closure = closures_[i];
      GRPC_CALL_COMBINER_START(call_combiner, closure.closure, closure.error,
                               closure.reason);
    }
    closures_.clear();
  }

  size_t size() const { return closures_.size(); }

 private:
  struct CallCombinerClosure {
    grpc_closure* closure;
    grpc_error* error;
    const char* reason;

    CallCombinerClosure(grpc_closure* closure, grpc_error* error,
                        const char* reason)
        : closure(closure), error(error), reason(reason) {}
  };

  // There are generally a maximum of 6 closures to run in the call
  // combiner, one for each pending op.
  InlinedVector<CallCombinerClosure, 6> closures_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H */
