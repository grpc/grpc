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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/completion_queue.h"

struct grpc_alarm {
  grpc_timer alarm;
  grpc_closure on_alarm;
  grpc_cq_completion completion;
  /** completion queue where events about this alarm will be posted */
  grpc_completion_queue *cq;
  /** user supplied tag */
  void *tag;
};

static void do_nothing_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_cq_completion *c) {}

static void alarm_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_alarm *alarm = arg;
  grpc_cq_end_op(exec_ctx, alarm->cq, alarm->tag, error,
                 do_nothing_end_completion, NULL, &alarm->completion);
}

grpc_alarm *grpc_alarm_create(grpc_completion_queue *cq, gpr_timespec deadline,
                              void *tag) {
  grpc_alarm *alarm = gpr_malloc(sizeof(grpc_alarm));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_CQ_INTERNAL_REF(cq, "alarm");
  alarm->cq = cq;
  alarm->tag = tag;

  grpc_cq_begin_op(cq, tag);
  GRPC_CLOSURE_INIT(&alarm->on_alarm, alarm_cb, alarm,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&exec_ctx, &alarm->alarm,
                  grpc_timespec_to_millis_round_up(deadline), &alarm->on_alarm);
  grpc_exec_ctx_finish(&exec_ctx);
  return alarm;
}

void grpc_alarm_cancel(grpc_alarm *alarm) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_timer_cancel(&exec_ctx, &alarm->alarm);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_alarm_destroy(grpc_alarm *alarm) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_alarm_cancel(alarm);
  GRPC_CQ_INTERNAL_UNREF(&exec_ctx, alarm->cq, "alarm");
  gpr_free(alarm);
  grpc_exec_ctx_finish(&exec_ctx);
}
