/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/completion_queue.h"

struct grpc_alarm {
  grpc_timer alarm;
  grpc_cq_completion completion;
  /** completion queue where events about this alarm will be posted */
  grpc_completion_queue *cq;
  /** user supplied tag */
  void *tag;
};

static void do_nothing_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_cq_completion *c) {}

static void alarm_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  grpc_alarm *alarm = arg;
  grpc_cq_end_op(exec_ctx, alarm->cq, alarm->tag, success,
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
  grpc_timer_init(&exec_ctx, &alarm->alarm,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  alarm_cb, alarm, gpr_now(GPR_CLOCK_MONOTONIC));
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
  GRPC_CQ_INTERNAL_UNREF(alarm->cq, "alarm");
  gpr_free(alarm);
}
