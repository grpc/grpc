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

#include "rb_completion_queue.h"

#include <ruby/ruby.h>
#include <ruby/thread.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include "rb_grpc.h"

/* grpc_rb_cCompletionQueue is the ruby class that proxies
 * grpc_completion_queue. */
static VALUE grpc_rb_cCompletionQueue = Qnil;

/* Used to allow grpc_completion_queue_next call to release the GIL */
typedef struct next_call_stack {
  grpc_completion_queue *cq;
  grpc_event event;
  gpr_timespec timeout;
  void *tag;
} next_call_stack;

/* Calls grpc_completion_queue_next without holding the ruby GIL */
static void *grpc_rb_completion_queue_next_no_gil(void *param) {
  next_call_stack *const next_call = (next_call_stack*)param;
  next_call->event =
      grpc_completion_queue_next(next_call->cq, next_call->timeout, NULL);
  return NULL;
}

/* Calls grpc_completion_queue_pluck without holding the ruby GIL */
static void *grpc_rb_completion_queue_pluck_no_gil(void *param) {
  next_call_stack *const next_call = (next_call_stack*)param;
  next_call->event = grpc_completion_queue_pluck(next_call->cq, next_call->tag,
                                                 next_call->timeout, NULL);
  return NULL;
}

/* Shuts down and drains the completion queue if necessary.
 *
 * This is done when the ruby completion queue object is about to be GCed.
 */
static void grpc_rb_completion_queue_shutdown_drain(grpc_completion_queue *cq) {
  next_call_stack next_call;
  grpc_completion_type type;
  int drained = 0;
  MEMZERO(&next_call, next_call_stack, 1);

  grpc_completion_queue_shutdown(cq);
  next_call.cq = cq;
  next_call.event.type = GRPC_QUEUE_TIMEOUT;
  /* TODO: the timeout should be a module level constant that defaults
   * to gpr_inf_future(GPR_CLOCK_REALTIME).
   *
   * - at the moment this does not work, it stalls.  Using a small timeout like
   *   this one works, and leads to fast test run times; a longer timeout was
   *   causing unnecessary delays in the test runs.
   *
   * - investigate further, this is probably another example of C-level cleanup
   * not working consistently in all cases.
   */
  next_call.timeout = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_micros(5e3, GPR_TIMESPAN));
  do {
    rb_thread_call_without_gvl(grpc_rb_completion_queue_next_no_gil,
                               (void *)&next_call, NULL, NULL);
    type = next_call.event.type;
    if (type == GRPC_QUEUE_TIMEOUT) break;
    if (type != GRPC_QUEUE_SHUTDOWN) {
      ++drained;
      rb_warning("completion queue shutdown: %d undrained events", drained);
    }
  } while (type != GRPC_QUEUE_SHUTDOWN);
}

/* Helper function to free a completion queue. */
static void grpc_rb_completion_queue_destroy(void *p) {
  grpc_completion_queue *cq = NULL;
  if (p == NULL) {
    return;
  }
  cq = (grpc_completion_queue *)p;
  grpc_rb_completion_queue_shutdown_drain(cq);
  grpc_completion_queue_destroy(cq);
}

static rb_data_type_t grpc_rb_completion_queue_data_type = {
    "grpc_completion_queue",
    {GRPC_RB_GC_NOT_MARKED, grpc_rb_completion_queue_destroy,
     GRPC_RB_MEMSIZE_UNAVAILABLE, {NULL, NULL}},
    NULL, NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    /* cannot immediately free because grpc_rb_completion_queue_shutdown_drain
     * calls rb_thread_call_without_gvl. */
    0,
#endif
};

/* Allocates a completion queue. */
static VALUE grpc_rb_completion_queue_alloc(VALUE cls) {
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  if (cq == NULL) {
    rb_raise(rb_eArgError, "could not create a completion queue: not sure why");
  }
  return TypedData_Wrap_Struct(cls, &grpc_rb_completion_queue_data_type, cq);
}

/* Blocks until the next event for given tag is available, and returns the
 * event. */
grpc_event grpc_rb_completion_queue_pluck_event(VALUE self, VALUE tag,
                                                VALUE timeout) {
  next_call_stack next_call;
  MEMZERO(&next_call, next_call_stack, 1);
  TypedData_Get_Struct(self, grpc_completion_queue,
                       &grpc_rb_completion_queue_data_type, next_call.cq);
  if (TYPE(timeout) == T_NIL) {
    next_call.timeout = gpr_inf_future(GPR_CLOCK_REALTIME);
  } else {
    next_call.timeout = grpc_rb_time_timeval(timeout, /* absolute time*/ 0);
  }
  if (TYPE(tag) == T_NIL) {
    next_call.tag = NULL;
  } else {
    next_call.tag = ROBJECT(tag);
  }
  next_call.event.type = GRPC_QUEUE_TIMEOUT;
  rb_thread_call_without_gvl(grpc_rb_completion_queue_pluck_no_gil,
                             (void *)&next_call, NULL, NULL);
  return next_call.event;
}

void Init_grpc_completion_queue() {
  grpc_rb_cCompletionQueue =
      rb_define_class_under(grpc_rb_mGrpcCore, "CompletionQueue", rb_cObject);

  /* constructor: uses an alloc func without an initializer. Using a simple
     alloc func works here as the grpc header does not specify any args for
     this func, so no separate initialization step is necessary. */
  rb_define_alloc_func(grpc_rb_cCompletionQueue,
                       grpc_rb_completion_queue_alloc);
}

/* Gets the wrapped completion queue from the ruby wrapper */
grpc_completion_queue *grpc_rb_get_wrapped_completion_queue(VALUE v) {
  grpc_completion_queue *cq = NULL;
  TypedData_Get_Struct(v, grpc_completion_queue,
                       &grpc_rb_completion_queue_data_type, cq);
  return cq;
}
