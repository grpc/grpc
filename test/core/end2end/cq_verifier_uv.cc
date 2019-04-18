/*
 *
 * Copyright 2016 gRPC authors.
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

#ifdef GRPC_UV

#include <uv.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/end2end/cq_verifier_internal.h"

typedef enum timer_state {
  TIMER_STARTED,
  TIMER_TRIGGERED,
  TIMER_CLOSED
} timer_state;

/* the verifier itself */
struct cq_verifier {
  /* bound completion queue */
  grpc_completion_queue* cq;
  /* start of expectation list */
  expectation* first_expectation;
  uv_timer_t timer;
};

cq_verifier* cq_verifier_create(grpc_completion_queue* cq) {
  cq_verifier* v = static_cast<cq_verifier*>(gpr_malloc(sizeof(cq_verifier)));
  v->cq = cq;
  v->first_expectation = NULL;
  uv_timer_init(uv_default_loop(), &v->timer);
  v->timer.data = (void*)TIMER_STARTED;
  return v;
}

static void timer_close_cb(uv_handle_t* handle) {
  handle->data = (void*)TIMER_CLOSED;
}

void cq_verifier_destroy(cq_verifier* v) {
  cq_verify(v);
  uv_close((uv_handle_t*)&v->timer, timer_close_cb);
  while (static_cast<timer_state>(v->timer.data) != TIMER_CLOSED) {
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  }
  gpr_free(v);
}

expectation* cq_verifier_get_first_expectation(cq_verifier* v) {
  return v->first_expectation;
}

void cq_verifier_set_first_expectation(cq_verifier* v, expectation* e) {
  v->first_expectation = e;
}

static void timer_run_cb(uv_timer_t* timer) {
  timer->data = (void*)TIMER_TRIGGERED;
}

grpc_event cq_verifier_next_event(cq_verifier* v, int timeout_seconds) {
  uint64_t timeout_ms =
      timeout_seconds < 0 ? 0 : (uint64_t)timeout_seconds * 1000;
  grpc_event ev;
  v->timer.data = (void*)TIMER_STARTED;
  uv_timer_start(&v->timer, timer_run_cb, timeout_ms, 0);
  ev = grpc_completion_queue_next(v->cq, gpr_inf_past(GPR_CLOCK_MONOTONIC),
                                  NULL);
  // Stop the loop if the timer goes off or we get a non-timeout event
  while ((static_cast<timer_state>(v->timer.data) != TIMER_TRIGGERED) &&
         ev.type == GRPC_QUEUE_TIMEOUT) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);
    ev = grpc_completion_queue_next(v->cq, gpr_inf_past(GPR_CLOCK_MONOTONIC),
                                    NULL);
  }
  return ev;
}

#endif /* GRPC_UV */
