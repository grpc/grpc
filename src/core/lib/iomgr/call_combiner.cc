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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/call_combiner.h"

#include <inttypes.h>

#include <grpc/support/log.h>
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/profiling/timers.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_call_combiner_trace(false, "call_combiner");

namespace {

grpc_error* DecodeCancelStateError(gpr_atm cancel_state) {
  if (cancel_state & 1) {
    return (grpc_error*)(cancel_state & ~static_cast<gpr_atm>(1));
  }
  return GRPC_ERROR_NONE;
}

gpr_atm EncodeCancelStateError(grpc_error* error) {
  return static_cast<gpr_atm>(1) | (gpr_atm)error;
}

}  // namespace

CallCombiner::CallCombiner() {
  gpr_atm_no_barrier_store(&cancel_state_, 0);
  gpr_atm_no_barrier_store(&size_, 0);
  gpr_mpscq_init(&queue_);
#ifdef GRPC_TSAN_ENABLED
  GRPC_CLOSURE_INIT(&tsan_closure_, TsanClosure, this,
                    grpc_schedule_on_exec_ctx);
#endif
}

CallCombiner::~CallCombiner() {
  gpr_mpscq_destroy(&queue_);
  GRPC_ERROR_UNREF(DecodeCancelStateError(cancel_state_));
}

#ifdef GRPC_TSAN_ENABLED
void CallCombiner::TsanClosure(void* arg, grpc_error* error) {
  CallCombiner* self = static_cast<CallCombiner*>(arg);
  // We ref-count the lock, and check if it's already taken.
  // If it was taken, we should do nothing. Otherwise, we will mark it as
  // locked. Note that if two different threads try to do this, only one of
  // them will be able to mark the lock as acquired, while they both run their
  // callbacks. In such cases (which should never happen for call_combiner),
  // TSAN will correctly produce an error.
  //
  // TODO(soheil): This only covers the callbacks scheduled by
  //               CallCombiner::Start() and CallCombiner::Stop().
  //               If in the future, a callback gets scheduled using other
  //               mechanisms, we will need to add APIs to externally lock
  //               call combiners.
  RefCountedPtr<TsanLock> lock = self->tsan_lock_;
  bool prev = false;
  if (lock->taken.compare_exchange_strong(prev, true)) {
    TSAN_ANNOTATE_RWLOCK_ACQUIRED(&lock->taken, true);
  } else {
    lock.reset();
  }
  GRPC_CLOSURE_RUN(self->original_closure_, GRPC_ERROR_REF(error));
  if (lock != nullptr) {
    TSAN_ANNOTATE_RWLOCK_RELEASED(&lock->taken, true);
    bool prev = true;
    GPR_ASSERT(lock->taken.compare_exchange_strong(prev, false));
  }
}
#endif

void CallCombiner::ScheduleClosure(grpc_closure* closure, grpc_error* error) {
#ifdef GRPC_TSAN_ENABLED
  original_closure_ = closure;
  GRPC_CLOSURE_SCHED(&tsan_closure_, error);
#else
  GRPC_CLOSURE_SCHED(closure, error);
#endif
}

#ifndef NDEBUG
#define DEBUG_ARGS const char *file, int line,
#define DEBUG_FMT_STR "%s:%d: "
#define DEBUG_FMT_ARGS , file, line
#else
#define DEBUG_ARGS
#define DEBUG_FMT_STR
#define DEBUG_FMT_ARGS
#endif

void CallCombiner::Start(grpc_closure* closure, grpc_error* error,
                         DEBUG_ARGS const char* reason) {
  GPR_TIMER_SCOPE("CallCombiner::Start", 0);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
    gpr_log(GPR_INFO,
            "==> CallCombiner::Start() [%p] closure=%p [" DEBUG_FMT_STR
            "%s] error=%s",
            this, closure DEBUG_FMT_ARGS, reason, grpc_error_string(error));
  }
  size_t prev_size =
      static_cast<size_t>(gpr_atm_full_fetch_add(&size_, (gpr_atm)1));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
    gpr_log(GPR_INFO, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
            prev_size + 1);
  }
  GRPC_STATS_INC_CALL_COMBINER_LOCKS_SCHEDULED_ITEMS();
  if (prev_size == 0) {
    GRPC_STATS_INC_CALL_COMBINER_LOCKS_INITIATED();
    GPR_TIMER_MARK("call_combiner_initiate", 0);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
      gpr_log(GPR_INFO, "  EXECUTING IMMEDIATELY");
    }
    // Queue was empty, so execute this closure immediately.
    ScheduleClosure(closure, error);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
      gpr_log(GPR_INFO, "  QUEUING");
    }
    // Queue was not empty, so add closure to queue.
    closure->error_data.error = error;
    gpr_mpscq_push(&queue_, reinterpret_cast<gpr_mpscq_node*>(closure));
  }
}

void CallCombiner::Stop(DEBUG_ARGS const char* reason) {
  GPR_TIMER_SCOPE("CallCombiner::Stop", 0);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
    gpr_log(GPR_INFO, "==> CallCombiner::Stop() [%p] [" DEBUG_FMT_STR "%s]",
            this DEBUG_FMT_ARGS, reason);
  }
  size_t prev_size =
      static_cast<size_t>(gpr_atm_full_fetch_add(&size_, (gpr_atm)-1));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
    gpr_log(GPR_INFO, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
            prev_size - 1);
  }
  GPR_ASSERT(prev_size >= 1);
  if (prev_size > 1) {
    while (true) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
        gpr_log(GPR_INFO, "  checking queue");
      }
      bool empty;
      grpc_closure* closure = reinterpret_cast<grpc_closure*>(
          gpr_mpscq_pop_and_check_end(&queue_, &empty));
      if (closure == nullptr) {
        // This can happen either due to a race condition within the mpscq
        // code or because of a race with Start().
        if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
          gpr_log(GPR_INFO, "  queue returned no result; checking again");
        }
        continue;
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
        gpr_log(GPR_INFO, "  EXECUTING FROM QUEUE: closure=%p error=%s",
                closure, grpc_error_string(closure->error_data.error));
      }
      ScheduleClosure(closure, closure->error_data.error);
      break;
    }
  } else if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
    gpr_log(GPR_INFO, "  queue empty");
  }
}

void CallCombiner::SetNotifyOnCancel(grpc_closure* closure) {
  GRPC_STATS_INC_CALL_COMBINER_SET_NOTIFY_ON_CANCEL();
  while (true) {
    // Decode original state.
    gpr_atm original_state = gpr_atm_acq_load(&cancel_state_);
    grpc_error* original_error = DecodeCancelStateError(original_state);
    // If error is set, invoke the cancellation closure immediately.
    // Otherwise, store the new closure.
    if (original_error != GRPC_ERROR_NONE) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
        gpr_log(GPR_INFO,
                "call_combiner=%p: scheduling notify_on_cancel callback=%p "
                "for pre-existing cancellation",
                this, closure);
      }
      GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_REF(original_error));
      break;
    } else {
      if (gpr_atm_full_cas(&cancel_state_, original_state, (gpr_atm)closure)) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
          gpr_log(GPR_INFO, "call_combiner=%p: setting notify_on_cancel=%p",
                  this, closure);
        }
        // If we replaced an earlier closure, invoke the original
        // closure with GRPC_ERROR_NONE.  This allows callers to clean
        // up any resources they may be holding for the callback.
        if (original_state != 0) {
          closure = (grpc_closure*)original_state;
          if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
            gpr_log(GPR_INFO,
                    "call_combiner=%p: scheduling old cancel callback=%p", this,
                    closure);
          }
          GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
        }
        break;
      }
    }
    // cas failed, try again.
  }
}

void CallCombiner::Cancel(grpc_error* error) {
  GRPC_STATS_INC_CALL_COMBINER_CANCELLED();
  while (true) {
    gpr_atm original_state = gpr_atm_acq_load(&cancel_state_);
    grpc_error* original_error = DecodeCancelStateError(original_state);
    if (original_error != GRPC_ERROR_NONE) {
      GRPC_ERROR_UNREF(error);
      break;
    }
    if (gpr_atm_full_cas(&cancel_state_, original_state,
                         EncodeCancelStateError(error))) {
      if (original_state != 0) {
        grpc_closure* notify_on_cancel = (grpc_closure*)original_state;
        if (GRPC_TRACE_FLAG_ENABLED(grpc_call_combiner_trace)) {
          gpr_log(GPR_INFO,
                  "call_combiner=%p: scheduling notify_on_cancel callback=%p",
                  this, notify_on_cancel);
        }
        GRPC_CLOSURE_SCHED(notify_on_cancel, GRPC_ERROR_REF(error));
      }
      break;
    }
    // cas failed, try again.
  }
}

}  // namespace grpc_core
