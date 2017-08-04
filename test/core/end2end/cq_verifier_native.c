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

/* This check is for testing only. */
#ifndef GRPC_UV

#include "test/core/end2end/cq_verifier_internal.h"

/* the verifier itself */
struct cq_verifier {
  /* bound completion queue */
  grpc_completion_queue *cq;
  /* start of expectation list */
  expectation *first_expectation;
  uv_timer_t timer;
};

cq_verifier *cq_verifier_create(grpc_completion_queue *cq) {
  cq_verifier *v = gpr_malloc(sizeof(cq_verifier));
  v->cq = cq;
  cq_verifier_set_first_expectation(v, NULL);
  return v;
}

void cq_verifier_destroy(cq_verifier *v) {
  cq_verify(v);
  gpr_free(v);
}

expectation *cq_verifier_get_first_expectation(cq_verifier *v) {
  return v->first_expectation;
}

void cq_verifier_set_first_expectation(cq_verifier *v, expectation *e) {
  v->first_expectation = e;
}

grpc_event cq_verifier_next_event(cq_verifier *v, int timeout_seconds) {
  const gpr_timespec deadline =
      grpc_timeout_seconds_to_deadline(timeout_seconds);
  return grpc_completion_queue_next(v->cq, deadline, NULL);
}

#endif /* GRPC_UV */
