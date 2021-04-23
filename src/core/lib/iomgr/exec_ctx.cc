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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/exec_ctx.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/profiling/timers.h"

static void exec_ctx_run(grpc_closure* closure, grpc_error_handle error) {
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

static void exec_ctx_sched(grpc_closure* closure, grpc_error_handle error) {
  grpc_closure_list_append(grpc_core::ExecCtx::Get()->closure_list(), closure,
                           error);
}

static gpr_timespec g_start_time;
static gpr_cycle_counter g_start_cycle;

static grpc_millis timespan_to_millis_round_down(gpr_timespec ts) {
  double x = GPR_MS_PER_SEC * static_cast<double>(ts.tv_sec) +
             static_cast<double>(ts.tv_nsec) / GPR_NS_PER_MS;
  if (x < 0) return 0;
  if (x > static_cast<double>(GRPC_MILLIS_INF_FUTURE)) {
    return GRPC_MILLIS_INF_FUTURE;
  }
  return static_cast<grpc_millis>(x);
}

static grpc_millis timespec_to_millis_round_down(gpr_timespec ts) {
  return timespan_to_millis_round_down(gpr_time_sub(ts, g_start_time));
}

static grpc_millis timespan_to_millis_round_up(gpr_timespec ts) {
  double x = GPR_MS_PER_SEC * static_cast<double>(ts.tv_sec) +
             static_cast<double>(ts.tv_nsec) / GPR_NS_PER_MS +
             static_cast<double>(GPR_NS_PER_SEC - 1) /
                 static_cast<double>(GPR_NS_PER_SEC);
  if (x < 0) return 0;
  if (x > static_cast<double>(GRPC_MILLIS_INF_FUTURE)) {
    return GRPC_MILLIS_INF_FUTURE;
  }
  return static_cast<grpc_millis>(x);
}

static grpc_millis timespec_to_millis_round_up(gpr_timespec ts) {
  return timespan_to_millis_round_up(gpr_time_sub(ts, g_start_time));
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
  return timespec_to_millis_round_down(
      gpr_convert_clock_type(ts, g_start_time.clock_type));
}

grpc_millis grpc_timespec_to_millis_round_up(gpr_timespec ts) {
  return timespec_to_millis_round_up(
      gpr_convert_clock_type(ts, g_start_time.clock_type));
}

grpc_millis grpc_cycle_counter_to_millis_round_down(gpr_cycle_counter cycles) {
  return timespan_to_millis_round_down(
      gpr_cycle_counter_sub(cycles, g_start_cycle));
}

grpc_millis grpc_cycle_counter_to_millis_round_up(gpr_cycle_counter cycles) {
  return timespan_to_millis_round_up(
      gpr_cycle_counter_sub(cycles, g_start_cycle));
}

namespace grpc_core {
GPR_TLS_CLASS_DEF(ExecCtx::exec_ctx_);
GPR_TLS_CLASS_DEF(ApplicationCallbackExecCtx::callback_exec_ctx_);

// WARNING: for testing purposes only!
void ExecCtx::TestOnlyGlobalInit(gpr_timespec new_val) {
  g_start_time = new_val;
  gpr_tls_init(&exec_ctx_);
}

void ExecCtx::GlobalInit(void) {
  // gpr_now(GPR_CLOCK_MONOTONIC) incurs a syscall. We don't actually know the
  // exact cycle the time was captured, so we use the average of cycles before
  // and after the syscall as the starting cycle.
  const gpr_cycle_counter cycle_before = gpr_get_cycle_counter();
  g_start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  const gpr_cycle_counter cycle_after = gpr_get_cycle_counter();
  g_start_cycle = (cycle_before + cycle_after) / 2;
  gpr_tls_init(&exec_ctx_);
}

bool ExecCtx::Flush() {
  bool did_something = false;
  GPR_TIMER_SCOPE("grpc_exec_ctx_flush", 0);
  for (;;) {
    if (!grpc_closure_list_empty(closure_list_)) {
      grpc_closure* c = closure_list_.head;
      closure_list_.head = closure_list_.tail = nullptr;
      while (c != nullptr) {
        grpc_closure* next = c->next_data.next;
        grpc_error_handle error = c->error_data.error;
        did_something = true;
        exec_ctx_run(c, error);
        c = next;
      }
    } else if (!grpc_combiner_continue_exec_ctx()) {
      break;
    }
  }
  GPR_ASSERT(combiner_data_.active_combiner == nullptr);
  return did_something;
}

grpc_millis ExecCtx::Now() {
  if (!now_is_valid_) {
    now_ = timespec_to_millis_round_down(gpr_now(GPR_CLOCK_MONOTONIC));
    now_is_valid_ = true;
  }
  return now_;
}

void ExecCtx::Run(const DebugLocation& location, grpc_closure* closure,
                  grpc_error_handle error) {
  (void)location;
  if (closure == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
#ifndef NDEBUG
  if (closure->scheduled) {
    gpr_log(GPR_ERROR,
            "Closure already scheduled. (closure: %p, created: [%s:%d], "
            "previously scheduled at: [%s: %d], newly scheduled at [%s: %d]",
            closure, closure->file_created, closure->line_created,
            closure->file_initiated, closure->line_initiated, location.file(),
            location.line());
    abort();
  }
  closure->scheduled = true;
  closure->file_initiated = location.file();
  closure->line_initiated = location.line();
  closure->run = false;
  GPR_ASSERT(closure->cb != nullptr);
#endif
  exec_ctx_sched(closure, error);
}

void ExecCtx::RunList(const DebugLocation& location, grpc_closure_list* list) {
  (void)location;
  grpc_closure* c = list->head;
  while (c != nullptr) {
    grpc_closure* next = c->next_data.next;
#ifndef NDEBUG
    if (c->scheduled) {
      gpr_log(GPR_ERROR,
              "Closure already scheduled. (closure: %p, created: [%s:%d], "
              "previously scheduled at: [%s: %d], newly scheduled at [%s:%d]",
              c, c->file_created, c->line_created, c->file_initiated,
              c->line_initiated, location.file(), location.line());
      abort();
    }
    c->scheduled = true;
    c->file_initiated = location.file();
    c->line_initiated = location.line();
    c->run = false;
    GPR_ASSERT(c->cb != nullptr);
#endif
    exec_ctx_sched(c, c->error_data.error);
    c = next;
  }
  list->head = list->tail = nullptr;
}

}  // namespace grpc_core
