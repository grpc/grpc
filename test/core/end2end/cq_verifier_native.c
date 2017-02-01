/*
 *
 * Copyright 2016, Google Inc.
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
