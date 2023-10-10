//
//
// Copyright 2016 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/combiner.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"

grpc_core::DebugOnlyTraceFlag grpc_combiner_trace(false, "combiner");

#define GRPC_COMBINER_TRACE(fn)          \
  do {                                   \
    if (grpc_combiner_trace.enabled()) { \
      fn;                                \
    }                                    \
  } while (0)

#define STATE_UNORPHANED 1
#define STATE_ELEM_COUNT_LOW_BIT 2

static void combiner_exec(grpc_core::Combiner* lock, grpc_closure* closure,
                          grpc_error_handle error);
static void combiner_finally_exec(grpc_core::Combiner* lock,
                                  grpc_closure* closure,
                                  grpc_error_handle error);

// TODO(ctiller): delete this when the combiner_offload_to_event_engine
// experiment is removed.
static void offload(void* arg, grpc_error_handle error);

grpc_core::Combiner* grpc_combiner_create(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  grpc_core::Combiner* lock = new grpc_core::Combiner();
  if (grpc_core::IsCombinerOffloadToEventEngineEnabled()) {
    lock->event_engine = event_engine;
  } else {
    GRPC_CLOSURE_INIT(&lock->offload, offload, lock, nullptr);
  }
  gpr_ref_init(&lock->refs, 1);
  gpr_atm_no_barrier_store(&lock->state, STATE_UNORPHANED);
  grpc_closure_list_init(&lock->final_list);
  GRPC_COMBINER_TRACE(gpr_log(GPR_INFO, "C:%p create", lock));
  return lock;
}

static void really_destroy(grpc_core::Combiner* lock) {
  GRPC_COMBINER_TRACE(gpr_log(GPR_INFO, "C:%p really_destroy", lock));
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->state) == 0);
  delete lock;
}

static void start_destroy(grpc_core::Combiner* lock) {
  gpr_atm old_state = gpr_atm_full_fetch_add(&lock->state, -STATE_UNORPHANED);
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_INFO, "C:%p really_destroy old_state=%" PRIdPTR, lock, old_state));
  if (old_state == 1) {
    really_destroy(lock);
  }
}

#ifndef NDEBUG
#define GRPC_COMBINER_DEBUG_SPAM(op, delta)                                \
  if (grpc_combiner_trace.enabled()) {                                     \
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,                            \
            "C:%p %s %" PRIdPTR " --> %" PRIdPTR " %s", lock, (op),        \
            gpr_atm_no_barrier_load(&lock->refs.count),                    \
            gpr_atm_no_barrier_load(&lock->refs.count) + (delta), reason); \
  }
#else
#define GRPC_COMBINER_DEBUG_SPAM(op, delta)
#endif

void grpc_combiner_unref(grpc_core::Combiner* lock GRPC_COMBINER_DEBUG_ARGS) {
  GRPC_COMBINER_DEBUG_SPAM("UNREF", -1);
  if (gpr_unref(&lock->refs)) {
    start_destroy(lock);
  }
}

grpc_core::Combiner* grpc_combiner_ref(
    grpc_core::Combiner* lock GRPC_COMBINER_DEBUG_ARGS) {
  GRPC_COMBINER_DEBUG_SPAM("  REF", 1);
  gpr_ref(&lock->refs);
  return lock;
}

static void push_last_on_exec_ctx(grpc_core::Combiner* lock) {
  lock->next_combiner_on_this_exec_ctx = nullptr;
  if (grpc_core::ExecCtx::Get()->combiner_data()->active_combiner == nullptr) {
    grpc_core::ExecCtx::Get()->combiner_data()->active_combiner =
        grpc_core::ExecCtx::Get()->combiner_data()->last_combiner = lock;
  } else {
    grpc_core::ExecCtx::Get()
        ->combiner_data()
        ->last_combiner->next_combiner_on_this_exec_ctx = lock;
    grpc_core::ExecCtx::Get()->combiner_data()->last_combiner = lock;
  }
}

static void push_first_on_exec_ctx(grpc_core::Combiner* lock) {
  lock->next_combiner_on_this_exec_ctx =
      grpc_core::ExecCtx::Get()->combiner_data()->active_combiner;
  grpc_core::ExecCtx::Get()->combiner_data()->active_combiner = lock;
  if (lock->next_combiner_on_this_exec_ctx == nullptr) {
    grpc_core::ExecCtx::Get()->combiner_data()->last_combiner = lock;
  }
}

static void combiner_exec(grpc_core::Combiner* lock, grpc_closure* cl,
                          grpc_error_handle error) {
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, STATE_ELEM_COUNT_LOW_BIT);
  GRPC_COMBINER_TRACE(gpr_log(GPR_INFO,
                              "C:%p grpc_combiner_execute c=%p last=%" PRIdPTR,
                              lock, cl, last));
  if (last == 1) {
    gpr_atm_no_barrier_store(
        &lock->initiating_exec_ctx_or_null,
        reinterpret_cast<gpr_atm>(grpc_core::ExecCtx::Get()));
    // first element on this list: add it to the list of combiner locks
    // executing within this exec_ctx
    push_last_on_exec_ctx(lock);
  } else {
    // there may be a race with setting here: if that happens, we may delay
    // offload for one or two actions, and that's fine
    gpr_atm initiator =
        gpr_atm_no_barrier_load(&lock->initiating_exec_ctx_or_null);
    if (initiator != 0 &&
        initiator != reinterpret_cast<gpr_atm>(grpc_core::ExecCtx::Get())) {
      gpr_atm_no_barrier_store(&lock->initiating_exec_ctx_or_null, 0);
    }
  }
  GPR_ASSERT(last & STATE_UNORPHANED);  // ensure lock has not been destroyed
  assert(cl->cb);
  cl->error_data.error = grpc_core::internal::StatusAllocHeapPtr(error);
  lock->queue.Push(cl->next_data.mpscq_node.get());
}

static void move_next() {
  grpc_core::ExecCtx::Get()->combiner_data()->active_combiner =
      grpc_core::ExecCtx::Get()
          ->combiner_data()
          ->active_combiner->next_combiner_on_this_exec_ctx;
  if (grpc_core::ExecCtx::Get()->combiner_data()->active_combiner == nullptr) {
    grpc_core::ExecCtx::Get()->combiner_data()->last_combiner = nullptr;
  }
}

static void offload(void* arg, grpc_error_handle /*error*/) {
  grpc_core::Combiner* lock = static_cast<grpc_core::Combiner*>(arg);
  push_last_on_exec_ctx(lock);
}

static void queue_offload(grpc_core::Combiner* lock) {
  move_next();
  // Make the combiner look uncontended by storing a non-null value here, so
  // that we don't immediately offload again.
  gpr_atm_no_barrier_store(&lock->initiating_exec_ctx_or_null, 1);
  GRPC_COMBINER_TRACE(gpr_log(GPR_INFO, "C:%p queue_offload", lock));
  if (grpc_core::IsCombinerOffloadToEventEngineEnabled()) {
    lock->event_engine->Run([lock] {
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
      grpc_core::ExecCtx exec_ctx(0);
      push_last_on_exec_ctx(lock);
      exec_ctx.Flush();
    });
  } else {
    grpc_core::Executor::Run(&lock->offload, absl::OkStatus());
  }
}

bool grpc_combiner_continue_exec_ctx() {
  grpc_core::Combiner* lock =
      grpc_core::ExecCtx::Get()->combiner_data()->active_combiner;
  if (lock == nullptr) {
    return false;
  }

  bool contended =
      gpr_atm_no_barrier_load(&lock->initiating_exec_ctx_or_null) == 0;

  GRPC_COMBINER_TRACE(gpr_log(GPR_INFO,
                              "C:%p grpc_combiner_continue_exec_ctx "
                              "contended=%d "
                              "exec_ctx_ready_to_finish=%d "
                              "time_to_execute_final_list=%d",
                              lock, contended,
                              grpc_core::ExecCtx::Get()->IsReadyToFinish(),
                              lock->time_to_execute_final_list));

  if (grpc_core::IsCombinerOffloadToEventEngineEnabled()) {
    // offload only if both (1) the combiner is contended and has more than one
    // closure to execute, and (2) the current execution context needs to finish
    // as soon as possible
    if (contended && grpc_core::ExecCtx::Get()->IsReadyToFinish()) {
      // this execution context wants to move on: schedule remaining work to be
      // picked up on the executor
      queue_offload(lock);
      return true;
    }
  } else {
    // TODO(ctiller): delete this when the combiner_offload_to_event_engine
    // experiment is removed.

    // offload only if all the following conditions are true:
    // 1. the combiner is contended and has more than one closure to execute
    // 2. the current execution context needs to finish as soon as possible
    // 3. the current thread is not a worker for any background poller
    // 4. the DEFAULT executor is threaded
    if (contended && grpc_core::ExecCtx::Get()->IsReadyToFinish() &&
        !grpc_iomgr_platform_is_any_background_poller_thread() &&
        grpc_core::Executor::IsThreadedDefault()) {
      // this execution context wants to move on: schedule remaining work to be
      // picked up on the executor
      queue_offload(lock);
      return true;
    }
  }

  if (!lock->time_to_execute_final_list ||
      // peek to see if something new has shown up, and execute that with
      // priority
      (gpr_atm_acq_load(&lock->state) >> 1) > 1) {
    grpc_core::MultiProducerSingleConsumerQueue::Node* n = lock->queue.Pop();
    GRPC_COMBINER_TRACE(
        gpr_log(GPR_INFO, "C:%p maybe_finish_one n=%p", lock, n));
    if (n == nullptr) {
      // queue is in an inconsistent state: use this as a cue that we should
      // go off and do something else for a while (and come back later)
      queue_offload(lock);
      return true;
    }
    grpc_closure* cl = reinterpret_cast<grpc_closure*>(n);
#ifndef NDEBUG
    cl->scheduled = false;
#endif
    grpc_error_handle cl_err =
        grpc_core::internal::StatusMoveFromHeapPtr(cl->error_data.error);
    cl->error_data.error = 0;
    cl->cb(cl->cb_arg, std::move(cl_err));
  } else {
    grpc_closure* c = lock->final_list.head;
    GPR_ASSERT(c != nullptr);
    grpc_closure_list_init(&lock->final_list);
    int loops = 0;
    while (c != nullptr) {
      GRPC_COMBINER_TRACE(
          gpr_log(GPR_INFO, "C:%p execute_final[%d] c=%p", lock, loops, c));
      grpc_closure* next = c->next_data.next;
#ifndef NDEBUG
      c->scheduled = false;
#endif
      grpc_error_handle error =
          grpc_core::internal::StatusMoveFromHeapPtr(c->error_data.error);
      c->error_data.error = 0;
      c->cb(c->cb_arg, std::move(error));
      c = next;
    }
  }

  move_next();
  lock->time_to_execute_final_list = false;
  gpr_atm old_state =
      gpr_atm_full_fetch_add(&lock->state, -STATE_ELEM_COUNT_LOW_BIT);
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_INFO, "C:%p finish old_state=%" PRIdPTR, lock, old_state));
// Define a macro to ease readability of the following switch statement.
#define OLD_STATE_WAS(orphaned, elem_count) \
  (((orphaned) ? 0 : STATE_UNORPHANED) |    \
   ((elem_count) * STATE_ELEM_COUNT_LOW_BIT))
  // Depending on what the previous state was, we need to perform different
  // actions.
  switch (old_state) {
    default:
      // we have multiple queued work items: just continue executing them
      break;
    case OLD_STATE_WAS(false, 2):
    case OLD_STATE_WAS(true, 2):
      // we're down to one queued item: if it's the final list we should do that
      if (!grpc_closure_list_empty(lock->final_list)) {
        lock->time_to_execute_final_list = true;
      }
      break;
    case OLD_STATE_WAS(false, 1):
      // had one count, one unorphaned --> unlocked unorphaned
      return true;
    case OLD_STATE_WAS(true, 1):
      // and one count, one orphaned --> unlocked and orphaned
      really_destroy(lock);
      return true;
    case OLD_STATE_WAS(false, 0):
    case OLD_STATE_WAS(true, 0):
      // these values are illegal - representing an already unlocked or
      // deleted lock
      GPR_UNREACHABLE_CODE(return true);
  }
  push_first_on_exec_ctx(lock);
  return true;
}

static void enqueue_finally(void* closure, grpc_error_handle error);

static void combiner_finally_exec(grpc_core::Combiner* lock,
                                  grpc_closure* closure,
                                  grpc_error_handle error) {
  GPR_ASSERT(lock != nullptr);
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_INFO, "C:%p grpc_combiner_execute_finally c=%p; ac=%p", lock, closure,
      grpc_core::ExecCtx::Get()->combiner_data()->active_combiner));
  if (grpc_core::ExecCtx::Get()->combiner_data()->active_combiner != lock) {
    // Using error_data.scratch to store the combiner so that it can be accessed
    // in enqueue_finally.
    closure->error_data.scratch = reinterpret_cast<uintptr_t>(lock);
    lock->Run(GRPC_CLOSURE_CREATE(enqueue_finally, closure, nullptr), error);
    return;
  }

  if (grpc_closure_list_empty(lock->final_list)) {
    gpr_atm_full_fetch_add(&lock->state, STATE_ELEM_COUNT_LOW_BIT);
  }
  grpc_closure_list_append(&lock->final_list, closure, error);
}

static void enqueue_finally(void* closure, grpc_error_handle error) {
  grpc_closure* cl = static_cast<grpc_closure*>(closure);
  grpc_core::Combiner* lock =
      reinterpret_cast<grpc_core::Combiner*>(cl->error_data.scratch);
  cl->error_data.scratch = 0;
  combiner_finally_exec(lock, cl, error);
}

namespace grpc_core {
void Combiner::Run(grpc_closure* closure, grpc_error_handle error) {
  combiner_exec(this, closure, error);
}

void Combiner::FinallyRun(grpc_closure* closure, grpc_error_handle error) {
  combiner_finally_exec(this, closure, error);
}

void Combiner::ForceOffload() {
  gpr_atm_no_barrier_store(&initiating_exec_ctx_or_null, 0);
  ExecCtx::Get()->SetReadyToFinishFlag();
}

}  // namespace grpc_core
