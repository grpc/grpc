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

#include "src/core/lib/iomgr/exec_ctx.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/profiling/timers.h"

#define GRPC_START_TIME_UPDATE_INTERVAL 10000
extern "C" grpc_tracer_flag grpc_timer_check_trace;

bool grpc_exec_ctx_ready_to_finish(grpc_exec_ctx* exec_ctx) {
  if ((exec_ctx->flags & GRPC_EXEC_CTX_FLAG_IS_FINISHED) == 0) {
    if (exec_ctx->check_ready_to_finish(exec_ctx,
                                        exec_ctx->check_ready_to_finish_arg)) {
      exec_ctx->flags |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
      return true;
    }
    return false;
  } else {
    return true;
  }
}

bool grpc_never_ready_to_finish(grpc_exec_ctx* exec_ctx, void* arg_ignored) {
  return false;
}

bool grpc_always_ready_to_finish(grpc_exec_ctx* exec_ctx, void* arg_ignored) {
  return true;
}

bool grpc_exec_ctx_has_work(grpc_exec_ctx* exec_ctx) {
  return exec_ctx->active_combiner != nullptr ||
         !grpc_closure_list_empty(exec_ctx->closure_list);
}

void grpc_exec_ctx_finish(grpc_exec_ctx* exec_ctx) {
  exec_ctx->flags |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
  grpc_exec_ctx_flush(exec_ctx);
}

static void exec_ctx_run(grpc_exec_ctx* exec_ctx, grpc_closure* closure,
                         grpc_error* error) {
#ifndef NDEBUG
  closure->scheduled = false;
  if (GRPC_TRACER_ON(grpc_trace_closure)) {
    gpr_log(GPR_DEBUG, "running closure %p: created [%s:%d]: %s [%s:%d]",
            closure, closure->file_created, closure->line_created,
            closure->run ? "run" : "scheduled", closure->file_initiated,
            closure->line_initiated);
  }
#endif
  closure->cb(exec_ctx, closure->cb_arg, error);
#ifndef NDEBUG
  if (GRPC_TRACER_ON(grpc_trace_closure)) {
    gpr_log(GPR_DEBUG, "closure %p finished", closure);
  }
#endif
  GRPC_ERROR_UNREF(error);
}

bool grpc_exec_ctx_flush(grpc_exec_ctx* exec_ctx) {
  bool did_something = 0;
  GPR_TIMER_BEGIN("grpc_exec_ctx_flush", 0);
  for (;;) {
    if (!grpc_closure_list_empty(exec_ctx->closure_list)) {
      grpc_closure* c = exec_ctx->closure_list.head;
      exec_ctx->closure_list.head = exec_ctx->closure_list.tail = nullptr;
      while (c != nullptr) {
        grpc_closure* next = c->next_data.next;
        grpc_error* error = c->error_data.error;
        did_something = true;
        exec_ctx_run(exec_ctx, c, error);
        c = next;
      }
    } else if (!grpc_combiner_continue_exec_ctx(exec_ctx)) {
      break;
    }
  }
  GPR_ASSERT(exec_ctx->active_combiner == nullptr);
  GPR_TIMER_END("grpc_exec_ctx_flush", 0);
  return did_something;
}

static void exec_ctx_sched(grpc_exec_ctx* exec_ctx, grpc_closure* closure,
                           grpc_error* error) {
  grpc_closure_list_append(&exec_ctx->closure_list, closure, error);
}

/* This time pair is not entirely thread-safe as store/load of tv_sec and
 * tv_nsec are performed separately. However g_start_time do not need to have
 * sub-second precision, so it is ok if the value of tv_nsec is off in this
 * case. */
typedef struct time_atm_pair {
  gpr_atm tv_sec;
  gpr_atm tv_nsec;
} time_atm_pair;

static time_atm_pair
    g_start_time[GPR_TIMESPAN + 1];  // assumes GPR_TIMESPAN is the
                                     // last enum value in
                                     // gpr_clock_type
static grpc_millis g_last_start_time_update;

static gpr_timespec timespec_from_time_atm_pair(const time_atm_pair* src,
                                                gpr_clock_type clock_type) {
  gpr_timespec time;
  time.tv_nsec = (int32_t)gpr_atm_no_barrier_load(&src->tv_nsec);
  time.tv_sec = (int64_t)gpr_atm_no_barrier_load(&src->tv_sec);
  time.clock_type = clock_type;
  return time;
}

static void time_atm_pair_store(time_atm_pair* dst, const gpr_timespec src) {
  gpr_atm_no_barrier_store(&dst->tv_sec, src.tv_sec);
  gpr_atm_no_barrier_store(&dst->tv_nsec, src.tv_nsec);
}

void grpc_exec_ctx_global_init(void) {
  for (int i = 0; i < GPR_TIMESPAN; i++) {
    time_atm_pair_store(&g_start_time[i], gpr_now((gpr_clock_type)i));
  }
  // allows uniform treatment in conversion functions
  time_atm_pair_store(&g_start_time[GPR_TIMESPAN], gpr_time_0(GPR_TIMESPAN));
}

void grpc_exec_ctx_global_shutdown(void) {}

static gpr_atm timespec_to_atm_round_down(gpr_timespec ts) {
  gpr_timespec start_time =
      timespec_from_time_atm_pair(&g_start_time[ts.clock_type], ts.clock_type);
  ts = gpr_time_sub(ts, start_time);
  double x =
      GPR_MS_PER_SEC * (double)ts.tv_sec + (double)ts.tv_nsec / GPR_NS_PER_MS;
  if (x < 0) return 0;
  if (x > GPR_ATM_MAX) return GPR_ATM_MAX;
  return (gpr_atm)x;
}

static gpr_atm timespec_to_atm_round_up(gpr_timespec ts) {
  gpr_timespec start_time =
      timespec_from_time_atm_pair(&g_start_time[ts.clock_type], ts.clock_type);
  ts = gpr_time_sub(ts, start_time);
  double x = GPR_MS_PER_SEC * (double)ts.tv_sec +
             (double)ts.tv_nsec / GPR_NS_PER_MS +
             (double)(GPR_NS_PER_SEC - 1) / (double)GPR_NS_PER_SEC;
  if (x < 0) return 0;
  if (x > GPR_ATM_MAX) return GPR_ATM_MAX;
  return (gpr_atm)x;
}

grpc_millis grpc_exec_ctx_now(grpc_exec_ctx* exec_ctx) {
  if (!exec_ctx->now_is_valid) {
    exec_ctx->now = timespec_to_atm_round_down(gpr_now(GPR_CLOCK_MONOTONIC));
    exec_ctx->now_is_valid = true;
  }
  return exec_ctx->now;
}

void grpc_exec_ctx_invalidate_now(grpc_exec_ctx* exec_ctx) {
  exec_ctx->now_is_valid = false;
}

gpr_timespec grpc_millis_to_timespec(grpc_millis millis,
                                     gpr_clock_type clock_type) {
  // special-case infinities as grpc_millis can be 32bit on some platforms
  // while gpr_time_from_millis always takes an int64_t.
  if (millis == GRPC_MILLIS_INF_FUTURE) {
    return gpr_inf_future(clock_type);
  }
  if (millis == GRPC_MILLIS_INF_PAST) {
    return gpr_inf_past(clock_type);
  }

  if (clock_type == GPR_TIMESPAN) {
    return gpr_time_from_millis(millis, GPR_TIMESPAN);
  }
  gpr_timespec start_time =
      timespec_from_time_atm_pair(&g_start_time[clock_type], clock_type);
  return gpr_time_add(start_time, gpr_time_from_millis(millis, GPR_TIMESPAN));
}

grpc_millis grpc_timespec_to_millis_round_down(gpr_timespec ts) {
  return timespec_to_atm_round_down(ts);
}

grpc_millis grpc_timespec_to_millis_round_up(gpr_timespec ts) {
  return timespec_to_atm_round_up(ts);
}

void grpc_exec_ctx_maybe_update_start_time(grpc_exec_ctx* exec_ctx) {
  grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  grpc_millis last_start_time_update =
      gpr_atm_no_barrier_load(&g_last_start_time_update);

  if (now > last_start_time_update &&
      now - last_start_time_update > GRPC_START_TIME_UPDATE_INTERVAL) {
    /* Get the current system time and subtract \a now from it, where \a now is
     * the relative time from grpc_init() from monotonic clock. This calibrates
     * the time when grpc_exec_ctx_global_init was called based on current
     * system clock. */
    gpr_atm_no_barrier_store(&g_last_start_time_update, now);
    gpr_timespec real_now = gpr_now(GPR_CLOCK_REALTIME);
    gpr_timespec real_start_time =
        gpr_time_sub(real_now, gpr_time_from_millis(now, GPR_TIMESPAN));
    time_atm_pair_store(&g_start_time[GPR_CLOCK_REALTIME], real_start_time);

    if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
      gpr_log(GPR_DEBUG, "Update realtime clock start time: %" PRId64 "s %dns",
              real_start_time.tv_sec, real_start_time.tv_nsec);
    }
  }
}

static const grpc_closure_scheduler_vtable exec_ctx_scheduler_vtable = {
    exec_ctx_run, exec_ctx_sched, "exec_ctx"};
static grpc_closure_scheduler exec_ctx_scheduler = {&exec_ctx_scheduler_vtable};
grpc_closure_scheduler* grpc_schedule_on_exec_ctx = &exec_ctx_scheduler;
