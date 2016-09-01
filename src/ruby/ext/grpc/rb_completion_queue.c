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

#include <ruby/ruby.h>

#include "rb_grpc_imports.generated.h"
#include "rb_completion_queue.h"

#include <ruby/thread.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpc/support/log.h>
#include "rb_grpc.h"

/* Used to allow grpc_completion_queue_next call to release the GIL */
typedef struct next_call_stack {
  grpc_completion_queue *cq;
  grpc_event event;
  gpr_timespec timeout;
  void *tag;
  volatile int interrupted;
} next_call_stack;

/* Calls grpc_completion_queue_pluck without holding the ruby GIL */
static void *grpc_rb_completion_queue_pluck_no_gil(void *param) {
  next_call_stack *const next_call = (next_call_stack*)param;
  gpr_timespec increment = gpr_time_from_millis(20, GPR_TIMESPAN);
  gpr_timespec deadline;
  do {
    deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), increment);
    next_call->event = grpc_completion_queue_pluck(next_call->cq,
                                                   next_call->tag,
                                                   deadline, NULL);
    if (next_call->event.type != GRPC_QUEUE_TIMEOUT ||
        gpr_time_cmp(deadline, next_call->timeout) > 0) {
      break;
    }
  } while (!next_call->interrupted);
  return NULL;
}

/* Helper function to free a completion queue. */
void grpc_rb_completion_queue_destroy(grpc_completion_queue *cq) {
  /* Every function that adds an event to a queue also synchronously plucks
     that event from the queue, and holds a reference to the Ruby object that
     holds the queue, so we only get to this point if all of those functions
     have completed, and the queue is empty */
  grpc_completion_queue_shutdown(cq);
  grpc_completion_queue_destroy(cq);
}

static void unblock_func(void *param) {
  next_call_stack *const next_call = (next_call_stack*)param;
  next_call->interrupted = 1;
}

/* Does the same thing as grpc_completion_queue_pluck, while properly releasing
   the GVL and handling interrupts */
grpc_event rb_completion_queue_pluck(grpc_completion_queue *queue, void *tag,
                                     gpr_timespec deadline, void *reserved) {
  next_call_stack next_call;
  MEMZERO(&next_call, next_call_stack, 1);
  next_call.cq = queue;
  next_call.timeout = deadline;
  next_call.tag = tag;
  next_call.event.type = GRPC_QUEUE_TIMEOUT;
  (void)reserved;
  /* Loop until we finish a pluck without an interruption. The internal
     pluck function runs either until it is interrupted or it gets an
     event, or time runs out.

     The basic reason we need this relatively complicated construction is that
     we need to re-acquire the GVL when an interrupt comes in, so that the ruby
     interpreter can do what it needs to do with the interrupt. But we also need
     to get back to plucking when the interrupt has been handled. */
  do {
    next_call.interrupted = 0;
    rb_thread_call_without_gvl(grpc_rb_completion_queue_pluck_no_gil,
                               (void *)&next_call, unblock_func,
                               (void *)&next_call);
    /* If an interrupt prevented pluck from returning useful information, then
       any plucks that did complete must have timed out */
  } while (next_call.interrupted &&
           next_call.event.type == GRPC_QUEUE_TIMEOUT);
  return next_call.event;
}
