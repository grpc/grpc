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
#include "src/core/lib/surface/alarm_internal.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/completion_queue.h"

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_alarm_refcount =
    GRPC_TRACER_INITIALIZER(false, "alarm_refcount");
#endif

struct grpc_alarm {
  gpr_refcount refs;
  grpc_timer alarm;
  grpc_closure on_alarm;
  grpc_cq_completion completion;
  /** completion queue where events about this alarm will be posted */
  grpc_completion_queue *cq;
  /** user supplied tag */
  void *tag;
};

static void alarm_ref(grpc_alarm *alarm) { gpr_ref(&alarm->refs); }

static void alarm_unref(grpc_alarm *alarm) {
  if (gpr_unref(&alarm->refs)) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_CQ_INTERNAL_UNREF(&exec_ctx, alarm->cq, "alarm");
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_free(alarm);
  }
}

#ifndef NDEBUG
static void alarm_ref_dbg(grpc_alarm *alarm, const char *reason,
                          const char *file, int line) {
  if (GRPC_TRACER_ON(grpc_trace_alarm_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&alarm->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "Alarm:%p  ref %" PRIdPTR " -> %" PRIdPTR " %s", alarm, val,
            val + 1, reason);
  }

  alarm_ref(alarm);
}

static void alarm_unref_dbg(grpc_alarm *alarm, const char *reason,
                            const char *file, int line) {
  if (GRPC_TRACER_ON(grpc_trace_alarm_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&alarm->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "Alarm:%p  Unref %" PRIdPTR " -> %" PRIdPTR " %s", alarm, val,
            val - 1, reason);
  }

  alarm_unref(alarm);
}
#endif

static void alarm_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_cq_completion *c) {
  grpc_alarm *alarm = arg;
  GRPC_ALARM_UNREF(alarm, "dequeue-end-op");
}

static void alarm_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_alarm *alarm = arg;

  /* We are queuing an op on completion queue. This means, the alarm's structure
     cannot be destroyed until the op is dequeued. Adding an extra ref
     here and unref'ing when the op is dequeued will achieve this */
  GRPC_ALARM_REF(alarm, "queue-end-op");
  grpc_cq_end_op(exec_ctx, alarm->cq, alarm->tag, error, alarm_end_completion,
                 (void *)alarm, &alarm->completion);
}

grpc_alarm *grpc_alarm_create(grpc_completion_queue *cq, gpr_timespec deadline,
                              void *tag) {
  grpc_alarm *alarm = gpr_malloc(sizeof(grpc_alarm));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  gpr_ref_init(&alarm->refs, 1);

#ifndef NDEBUG
  if (GRPC_TRACER_ON(grpc_trace_alarm_refcount)) {
    gpr_log(GPR_DEBUG, "Alarm:%p created (ref: 1)", alarm);
  }
#endif

  GRPC_CQ_INTERNAL_REF(cq, "alarm");
  alarm->cq = cq;
  alarm->tag = tag;

  GPR_ASSERT(grpc_cq_begin_op(cq, tag));
  GRPC_CLOSURE_INIT(&alarm->on_alarm, alarm_cb, alarm,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&exec_ctx, &alarm->alarm,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  &alarm->on_alarm, gpr_now(GPR_CLOCK_MONOTONIC));
  grpc_exec_ctx_finish(&exec_ctx);
  return alarm;
}

void grpc_alarm_cancel(grpc_alarm *alarm) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_timer_cancel(&exec_ctx, &alarm->alarm);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_alarm_destroy(grpc_alarm *alarm) {
  grpc_alarm_cancel(alarm);
  GRPC_ALARM_UNREF(alarm, "alarm_destroy");
}
