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
#include <grpc/support/log.h>
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

static void *tag(gpr_intptr i) { return (void *)i; }

int main(int argc, char **argv) {
  grpc_channel *chan;
  grpc_call *call;
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2);
  grpc_completion_queue *cq;
  cq_verifier *cqv;
  grpc_event *ev;
  int done;

  grpc_test_init(argc, argv);
  grpc_init();

  cq = grpc_completion_queue_create();
  cqv = cq_verifier_create(cq);

  /* create a call, channel to a non existant server */
  chan = grpc_channel_create("nonexistant:54321", NULL);
  call = grpc_channel_create_call_old(chan, "/foo", "nonexistant", deadline);
  GPR_ASSERT(grpc_call_invoke_old(call, cq, tag(2), tag(3), 0) == GRPC_CALL_OK);
  /* verify that all tags get completed */
  cq_expect_client_metadata_read(cqv, tag(2), NULL);
  cq_expect_finished_with_status(cqv, tag(3), GRPC_STATUS_DEADLINE_EXCEEDED,
                                 "Deadline Exceeded", NULL);
  cq_verify(cqv);

  grpc_completion_queue_shutdown(cq);
  for (done = 0; !done;) {
    ev = grpc_completion_queue_next(cq, gpr_inf_future);
    done = ev->type == GRPC_QUEUE_SHUTDOWN;
    grpc_event_finish(ev);
  }
  grpc_completion_queue_destroy(cq);
  grpc_call_destroy(call);
  grpc_channel_destroy(chan);
  cq_verifier_destroy(cqv);

  grpc_shutdown();

  return 0;
}
