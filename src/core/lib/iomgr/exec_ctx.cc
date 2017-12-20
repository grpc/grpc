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

static void exec_ctx_run(grpc_closure* closure, grpc_error* error) {
#ifndef NDEBUG
  closure->scheduled = false;
  if (grpc_trace_closure.enabled()) {
    gpr_log(GPR_DEBUG, "running closure %p: created [%s:%d]: %s [%s:%d]",
            closure, closure->file_created, closure->line_created,
            closure->run ? "run" : "scheduled", closure->file_initiated,
            closure->line_initiated);
  }
#endif
  closure->cb(closure->cb_arg, error);
#ifndef NDEBUG
  if (grpc_trace_closure.enabled()) {
    gpr_log(GPR_DEBUG, "closure %p finished", closure);
  }
#endif
  GRPC_ERROR_UNREF(error);
}

static void exec_ctx_sched(grpc_closure* closure, grpc_error* error) {
  grpc_closure_list_append(grpc_core::ExecCtx::Get()->closure_list(), closure,
                           error);
}

static gpr_timespec g_start_time;

static gpr_atm timespec_to_atm_round_down(gpr_timespec ts) {
  ts = gpr_time_sub(ts, g_start_time);
  double x =
      GPR_MS_PER_SEC * (double)ts.tv_sec + (double)ts.tv_nsec / GPR_NS_PER_MS;
  if (x < 0) return 0;
  if (x > GPR_ATM_MAX) return GPR_ATM_MAX;
  return (gpr_atm)x;
}

static gpr_atm timespec_to_atm_round_up(gpr_timespec ts) {
  ts = gpr_time_sub(ts, g_start_time);
  double x = GPR_MS_PER_SEC * (double)ts.tv_sec +
             (double)ts.tv_nsec / GPR_NS_PER_MS +
             (double)(GPR_NS_PER_SEC - 1) / (double)GPR_NS_PER_SEC;
  if (x < 0) return 0;
  if (x > GPR_ATM_MAX) return GPR_ATM_MAX;
  return (gpr_atm)x;
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
  return gpr_time_add(gpr_convert_clock_type(g_start_time, clock_type),
                      gpr_time_from_millis(millis, GPR_TIMESPAN));
}

grpc_millis grpc_timespec_to_millis_round_down(gpr_timespec ts) {
  return timespec_to_atm_round_down(
      gpr_convert_clock_type(ts, g_start_time.clock_type));
}

grpc_millis grpc_timespec_to_millis_round_up(gpr_timespec ts) {
  return timespec_to_atm_round_up(
      gpr_convert_clock_type(ts, g_start_time.clock_type));
}

static const grpc_closure_scheduler_vtable exec_ctx_scheduler_vtable = {
    exec_ctx_run, exec_ctx_sched, "exec_ctx"};
static grpc_closure_scheduler exec_ctx_scheduler = {&exec_ctx_scheduler_vtable};
grpc_closure_scheduler* grpc_schedule_on_exec_ctx = &exec_ctx_scheduler;

namespace grpc_core {
GPR_TLS_CLASS_DEF(ExecCtx::exec_ctx_);

void ExecCtx::GlobalInit(void) {
  g_start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_tls_init(&exec_ctx_);
}

bool ExecCtx::Flush() {
  bool did_something = 0;
  GPR_TIMER_BEGIN("grpc_exec_ctx_flush", 0);
  for (;;) {
    if (!grpc_closure_list_empty(closure_list_)) {
      grpc_closure* c = closure_list_.head;
      closure_list_.head = closure_list_.tail = nullptr;
      while (c != nullptr) {
        grpc_closure* next = c->next_data.next;
        grpc_error* error = c->error_data.error;
        did_something = true;
        exec_ctx_run(c, error);
        c = next;
      }
    } else if (!grpc_combiner_continue_exec_ctx()) {
      break;
    }
  }
  GPR_ASSERT(combiner_data_.active_combiner == nullptr);
  GPR_TIMER_END("grpc_exec_ctx_flush", 0);
  return did_something;
}

grpc_millis ExecCtx::Now() {
  if (!now_is_valid_) {
    now_ = timespec_to_atm_round_down(gpr_now(GPR_CLOCK_MONOTONIC));
    now_is_valid_ = true;
  }
  return now_;
}

}  // namespace grpc_core
