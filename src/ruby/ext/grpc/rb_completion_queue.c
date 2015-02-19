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

#include <ruby.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include "rb_grpc.h"
#include "rb_event.h"

/* Used to allow grpc_completion_queue_next call to release the GIL */
typedef struct next_call_stack {
  grpc_completion_queue *cq;
  grpc_event *event;
  gpr_timespec timeout;
  void *tag;
} next_call_stack;

/* Calls grpc_completion_queue_next without holding the ruby GIL */
static void *grpc_rb_completion_queue_next_no_gil(next_call_stack *next_call) {
  next_call->event =
      grpc_completion_queue_next(next_call->cq, next_call->timeout);
  return NULL;
}

/* Calls grpc_completion_queue_pluck without holding the ruby GIL */
static void *grpc_rb_completion_queue_pluck_no_gil(next_call_stack *next_call) {
  next_call->event = grpc_completion_queue_pluck(next_call->cq, next_call->tag,
                                                 next_call->timeout);
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
  next_call.event = NULL;
  /* TODO: the timeout should be a module level constant that defaults
   * to gpr_inf_future.
   *
   * - at the moment this does not work, it stalls.  Using a small timeout like
   *   this one works, and leads to fast test run times; a longer timeout was
   *   causing unnecessary delays in the test runs.
   *
   * - investigate further, this is probably another example of C-level cleanup
   * not working consistently in all cases.
   */
  next_call.timeout = gpr_time_add(gpr_now(), gpr_time_from_micros(5e3));
  do {
    rb_thread_call_without_gvl(grpc_rb_completion_queue_next_no_gil,
                               (void *)&next_call, NULL, NULL);
    if (next_call.event == NULL) {
      break;
    }
    type = next_call.event->type;
    if (type != GRPC_QUEUE_SHUTDOWN) {
      ++drained;
      rb_warning("completion queue shutdown: %d undrained events", drained);
    }
    grpc_event_finish(next_call.event);
    next_call.event = NULL;
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

/* Allocates a completion queue. */
static VALUE grpc_rb_completion_queue_alloc(VALUE cls) {
  grpc_completion_queue *cq = grpc_completion_queue_create();
  if (cq == NULL) {
    rb_raise(rb_eArgError, "could not create a completion queue: not sure why");
  }
  return Data_Wrap_Struct(cls, GC_NOT_MARKED, grpc_rb_completion_queue_destroy,
                          cq);
}

/* Blocks until the next event is available, and returns the event. */
static VALUE grpc_rb_completion_queue_next(VALUE self, VALUE timeout) {
  next_call_stack next_call;
  MEMZERO(&next_call, next_call_stack, 1);
  Data_Get_Struct(self, grpc_completion_queue, next_call.cq);
  next_call.timeout = grpc_rb_time_timeval(timeout, /* absolute time*/ 0);
  next_call.event = NULL;
  rb_thread_call_without_gvl(grpc_rb_completion_queue_next_no_gil,
                             (void *)&next_call, NULL, NULL);
  if (next_call.event == NULL) {
    return Qnil;
  }
  return grpc_rb_new_event(next_call.event);
}

/* Blocks until the next event for given tag is available, and returns the
 * event. */
static VALUE grpc_rb_completion_queue_pluck(VALUE self, VALUE tag,
                                            VALUE timeout) {
  next_call_stack next_call;
  MEMZERO(&next_call, next_call_stack, 1);
  Data_Get_Struct(self, grpc_completion_queue, next_call.cq);
  next_call.timeout = grpc_rb_time_timeval(timeout, /* absolute time*/ 0);
  next_call.tag = ROBJECT(tag);
  next_call.event = NULL;
  rb_thread_call_without_gvl(grpc_rb_completion_queue_pluck_no_gil,
                             (void *)&next_call, NULL, NULL);
  if (next_call.event == NULL) {
    return Qnil;
  }
  return grpc_rb_new_event(next_call.event);
}

/* rb_cCompletionQueue is the ruby class that proxies grpc_completion_queue. */
VALUE rb_cCompletionQueue = Qnil;

void Init_grpc_completion_queue() {
  rb_cCompletionQueue =
      rb_define_class_under(rb_mGrpcCore, "CompletionQueue", rb_cObject);

  /* constructor: uses an alloc func without an initializer. Using a simple
     alloc func works here as the grpc header does not specify any args for
     this func, so no separate initialization step is necessary. */
  rb_define_alloc_func(rb_cCompletionQueue, grpc_rb_completion_queue_alloc);

  /* Add the next method that waits for the next event. */
  rb_define_method(rb_cCompletionQueue, "next", grpc_rb_completion_queue_next,
                   1);

  /* Add the pluck method that waits for the next event of given tag */
  rb_define_method(rb_cCompletionQueue, "pluck", grpc_rb_completion_queue_pluck,
                   2);
}

/* Gets the wrapped completion queue from the ruby wrapper */
grpc_completion_queue *grpc_rb_get_wrapped_completion_queue(VALUE v) {
  grpc_completion_queue *cq = NULL;
  Data_Get_Struct(v, grpc_completion_queue, cq);
  return cq;
}
