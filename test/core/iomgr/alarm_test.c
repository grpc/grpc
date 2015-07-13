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

/* Test gRPC event manager with a simple TCP upload server and client. */
#include "src/core/iomgr/alarm.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

#define SUCCESS_NOT_SET (-1)

/* Dummy gRPC callback */
void no_op_cb(void *arg, int success) {}

typedef struct {
  gpr_cv cv;
  gpr_mu mu;
  grpc_iomgr_closure *followup_closure;
  int counter;
  int done_success_ctr;
  int done_cancel_ctr;
  int done;
  gpr_event fcb_arg;
  int success;
} alarm_arg;

static void followup_cb(void *arg, int success) {
  gpr_event_set((gpr_event *)arg, arg);
}

/* Called when an alarm expires. */
static void alarm_cb(void *arg /* alarm_arg */, int success) {
  alarm_arg *a = arg;
  gpr_mu_lock(&a->mu);
  if (success) {
    a->counter++;
    a->done_success_ctr++;
  } else {
    a->done_cancel_ctr++;
  }
  a->done = 1;
  a->success = success;
  gpr_cv_signal(&a->cv);
  gpr_mu_unlock(&a->mu);
  grpc_iomgr_closure_init(a->followup_closure, followup_cb, &a->fcb_arg);
  grpc_iomgr_add_callback(a->followup_closure);
}

/* Test grpc_alarm add and cancel. */
static void test_grpc_alarm(void) {
  grpc_alarm alarm;
  grpc_alarm alarm_to_cancel;
  /* Timeout on the alarm cond. var, so make big enough to absorb time
     deviations. Otherwise, operations after wait will not be properly ordered
   */
  gpr_timespec alarm_deadline;
  gpr_timespec followup_deadline;

  alarm_arg arg;
  alarm_arg arg2;
  void *fdone;

  grpc_iomgr_init();

  arg.counter = 0;
  arg.success = SUCCESS_NOT_SET;
  arg.done_success_ctr = 0;
  arg.done_cancel_ctr = 0;
  arg.done = 0;
  gpr_mu_init(&arg.mu);
  gpr_cv_init(&arg.cv);
  arg.followup_closure = gpr_malloc(sizeof(grpc_iomgr_closure));
  gpr_event_init(&arg.fcb_arg);

  grpc_alarm_init(&alarm, GRPC_TIMEOUT_MILLIS_TO_DEADLINE(100), alarm_cb, &arg,
                  gpr_now(GPR_CLOCK_REALTIME));

  alarm_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1);
  gpr_mu_lock(&arg.mu);
  while (arg.done == 0) {
    if (gpr_cv_wait(&arg.cv, &arg.mu, alarm_deadline)) {
      gpr_log(GPR_ERROR, "alarm deadline exceeded");
      break;
    }
  }
  gpr_mu_unlock(&arg.mu);

  followup_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  fdone = gpr_event_wait(&arg.fcb_arg, followup_deadline);

  if (arg.counter != 1) {
    gpr_log(GPR_ERROR, "Alarm callback not called");
    GPR_ASSERT(0);
  } else if (arg.done_success_ctr != 1) {
    gpr_log(GPR_ERROR, "Alarm done callback not called with success");
    GPR_ASSERT(0);
  } else if (arg.done_cancel_ctr != 0) {
    gpr_log(GPR_ERROR, "Alarm done callback called with cancel");
    GPR_ASSERT(0);
  } else if (arg.success == SUCCESS_NOT_SET) {
    gpr_log(GPR_ERROR, "Alarm callback without status");
    GPR_ASSERT(0);
  } else {
    gpr_log(GPR_INFO, "Alarm callback called successfully");
  }

  if (fdone != (void *)&arg.fcb_arg) {
    gpr_log(GPR_ERROR, "Followup callback #1 not invoked properly %p %p", fdone,
            &arg.fcb_arg);
    GPR_ASSERT(0);
  }
  gpr_cv_destroy(&arg.cv);
  gpr_mu_destroy(&arg.mu);
  gpr_free(arg.followup_closure);

  arg2.counter = 0;
  arg2.success = SUCCESS_NOT_SET;
  arg2.done_success_ctr = 0;
  arg2.done_cancel_ctr = 0;
  arg2.done = 0;
  gpr_mu_init(&arg2.mu);
  gpr_cv_init(&arg2.cv);
  arg2.followup_closure = gpr_malloc(sizeof(grpc_iomgr_closure));

  gpr_event_init(&arg2.fcb_arg);

  grpc_alarm_init(&alarm_to_cancel, GRPC_TIMEOUT_MILLIS_TO_DEADLINE(100),
                  alarm_cb, &arg2, gpr_now(GPR_CLOCK_REALTIME));
  grpc_alarm_cancel(&alarm_to_cancel);

  alarm_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1);
  gpr_mu_lock(&arg2.mu);
  while (arg2.done == 0) {
    gpr_cv_wait(&arg2.cv, &arg2.mu, alarm_deadline);
  }
  gpr_mu_unlock(&arg2.mu);

  gpr_log(GPR_INFO, "alarm done = %d", arg2.done);

  followup_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  fdone = gpr_event_wait(&arg2.fcb_arg, followup_deadline);

  if (arg2.counter != arg2.done_success_ctr) {
    gpr_log(GPR_ERROR, "Alarm callback called but didn't lead to done success");
    GPR_ASSERT(0);
  } else if (arg2.done_success_ctr && arg2.done_cancel_ctr) {
    gpr_log(GPR_ERROR, "Alarm done callback called with success and cancel");
    GPR_ASSERT(0);
  } else if (arg2.done_cancel_ctr + arg2.done_success_ctr != 1) {
    gpr_log(GPR_ERROR, "Alarm done callback called incorrect number of times");
    GPR_ASSERT(0);
  } else if (arg2.success == SUCCESS_NOT_SET) {
    gpr_log(GPR_ERROR, "Alarm callback without status");
    GPR_ASSERT(0);
  } else if (arg2.done_success_ctr) {
    gpr_log(GPR_INFO, "Alarm callback executed before cancel");
    gpr_log(GPR_INFO, "Current value of triggered is %d\n",
            alarm_to_cancel.triggered);
  } else if (arg2.done_cancel_ctr) {
    gpr_log(GPR_INFO, "Alarm callback canceled");
    gpr_log(GPR_INFO, "Current value of triggered is %d\n",
            alarm_to_cancel.triggered);
  } else {
    gpr_log(GPR_ERROR, "Alarm cancel test should not be here");
    GPR_ASSERT(0);
  }

  if (fdone != (void *)&arg2.fcb_arg) {
    gpr_log(GPR_ERROR, "Followup callback #2 not invoked properly %p %p", fdone,
            &arg2.fcb_arg);
    GPR_ASSERT(0);
  }
  gpr_cv_destroy(&arg2.cv);
  gpr_mu_destroy(&arg2.mu);
  gpr_free(arg2.followup_closure);

  grpc_iomgr_shutdown();
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_grpc_alarm();
  return 0;
}
