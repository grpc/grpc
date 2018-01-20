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

#ifndef GRPC_CORE_LIB_IOMGR_EXEC_CTX_H
#define GRPC_CORE_LIB_IOMGR_EXEC_CTX_H

#include <grpc/support/atm.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/tls.h>

#include "src/core/lib/iomgr/closure.h"

typedef gpr_atm grpc_millis;

#define GRPC_MILLIS_INF_FUTURE GPR_ATM_MAX
#define GRPC_MILLIS_INF_PAST GPR_ATM_MIN

/** A workqueue represents a list of work to be executed asynchronously.
    Forward declared here to avoid a circular dependency with workqueue.h. */
typedef struct grpc_workqueue grpc_workqueue;
typedef struct grpc_combiner grpc_combiner;

/* This exec_ctx is ready to return: either pre-populated, or cached as soon as
   the finish_check returns true */
#define GRPC_EXEC_CTX_FLAG_IS_FINISHED 1
/* The exec_ctx's thread is (potentially) owned by a call or channel: care
   should be given to not delete said call/channel from this exec_ctx */
#define GRPC_EXEC_CTX_FLAG_THREAD_RESOURCE_LOOP 2

extern grpc_closure_scheduler* grpc_schedule_on_exec_ctx;

gpr_timespec grpc_millis_to_timespec(grpc_millis millis, gpr_clock_type clock);
grpc_millis grpc_timespec_to_millis_round_down(gpr_timespec timespec);
grpc_millis grpc_timespec_to_millis_round_up(gpr_timespec timespec);

namespace grpc_core {
/** Execution context.
 *  A bag of data that collects information along a callstack.
 *  Generally created at public API entry points, and passed down as
 *  pointer to child functions that manipulate it.
 *
 *  Specific responsibilities (this may grow in the future):
 *  - track a list of work that needs to be delayed until the top of the
 *    call stack (this provides a convenient mechanism to run callbacks
 *    without worrying about locking issues)
 *  - provide a decision maker (via grpc_exec_ctx_ready_to_finish) that provides
 *    signal as to whether a borrowed thread should continue to do work or
 *    should actively try to finish up and get this thread back to its owner
 *
 *  CONVENTIONS:
 *  - Instance of this must ALWAYS be constructed on the stack, never
 *    heap allocated.
 *  - Instances and pointers to them must always be called exec_ctx.
 *  - Instances are always passed as the first argument to a function that
 *    takes it, and always as a pointer (grpc_exec_ctx is never copied).
 */
class ExecCtx {
 public:
  /** Default Constructor */

  ExecCtx() : flags_(GRPC_EXEC_CTX_FLAG_IS_FINISHED) { Set(this); }

  /** Parameterised Constructor */
  ExecCtx(uintptr_t fl) : flags_(fl) { Set(this); }

  /** Destructor */
  virtual ~ExecCtx() {
    flags_ |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
    Flush();
    Set(last_exec_ctx_);
  }

  /** Disallow copy and assignment operators */
  ExecCtx(const ExecCtx&) = delete;
  ExecCtx& operator=(const ExecCtx&) = delete;

  /** Return starting_cpu */
  unsigned starting_cpu() const { return starting_cpu_; }

  struct CombinerData {
    /* currently active combiner: updated only via combiner.c */
    grpc_combiner* active_combiner;
    /* last active combiner in the active combiner list */
    grpc_combiner* last_combiner;
  };

  /** Only to be used by grpc-combiner code */
  CombinerData* combiner_data() { return &combiner_data_; }

  /** Return pointer to grpc_closure_list */
  grpc_closure_list* closure_list() { return &closure_list_; }

  /** Return flags */
  uintptr_t flags() { return flags_; }

  /** Checks if there is work to be done */
  bool HasWork() {
    return combiner_data_.active_combiner != nullptr ||
           !grpc_closure_list_empty(closure_list_);
  }

  /** Flush any work that has been enqueued onto this grpc_exec_ctx.
   *  Caller must guarantee that no interfering locks are held.
   *  Returns true if work was performed, false otherwise. */
  bool Flush();

  /** Returns true if we'd like to leave this execution context as soon as
possible: useful for deciding whether to do something more or not depending
on outside context */
  bool IsReadyToFinish() {
    if ((flags_ & GRPC_EXEC_CTX_FLAG_IS_FINISHED) == 0) {
      if (CheckReadyToFinish()) {
        flags_ |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
        return true;
      }
      return false;
    } else {
      return true;
    }
  }

  /** Returns the stored current time relative to start if valid,
   * otherwise refreshes the stored time, sets it valid and returns the new
   * value */
  grpc_millis Now();

  /** Invalidates the stored time value. A new time value will be set on calling
   * Now() */
  void InvalidateNow() { now_is_valid_ = false; }

  /** To be used only by shutdown code in iomgr */
  void SetNowIomgrShutdown() {
    now_ = GRPC_MILLIS_INF_FUTURE;
    now_is_valid_ = true;
  }

  /** To be used only for testing.
   * Sets the now value
   */
  void TestOnlySetNow(grpc_millis new_val) {
    now_ = new_val;
    now_is_valid_ = true;
  }

  /** Global initialization for ExecCtx. Called by iomgr */
  static void GlobalInit(void);

  /** Global shutdown for ExecCtx. Called by iomgr */
  static void GlobalShutdown(void) { gpr_tls_destroy(&exec_ctx_); }

  /** Gets pointer to current exec_ctx */
  static ExecCtx* Get() {
    return reinterpret_cast<ExecCtx*>(gpr_tls_get(&exec_ctx_));
  }

 protected:
  /** Check if ready to finish */
  virtual bool CheckReadyToFinish() { return false; }

  /** Disallow delete on ExecCtx */
  static void operator delete(void* p) { abort(); }

 private:
  /** Set exec_ctx_ to exec_ctx */
  void Set(ExecCtx* exec_ctx) {
    gpr_tls_set(&exec_ctx_, reinterpret_cast<intptr_t>(exec_ctx));
  }

  grpc_closure_list closure_list_ = GRPC_CLOSURE_LIST_INIT;
  CombinerData combiner_data_ = {nullptr, nullptr};
  uintptr_t flags_;
  unsigned starting_cpu_ = gpr_cpu_current_cpu();

  bool now_is_valid_ = false;
  grpc_millis now_ = 0;

  GPR_TLS_CLASS_DECL(exec_ctx_);
  ExecCtx* last_exec_ctx_ = Get();
};
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_EXEC_CTX_H */
