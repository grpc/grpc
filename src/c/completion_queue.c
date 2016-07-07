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
#include "completion_queue.h"
#include "call_ops.h"

GRPC_completion_queue *GRPC_completion_queue_create() {
  return grpc_completion_queue_create(NULL);
}

void GRPC_completion_queue_shutdown(GRPC_completion_queue *cq) {
  grpc_completion_queue_shutdown(cq);
}

void GRPC_completion_queue_destroy(GRPC_completion_queue *cq) {
  grpc_completion_queue_destroy(cq);
}

void GRPC_completion_queue_shutdown_and_destroy(GRPC_completion_queue *cq) {
  grpc_completion_queue_shutdown(cq);
  for (;;) {
    void *tag;
    bool ok;
    if (GRPC_commit_ops_and_wait(cq, &tag, &ok) == GRPC_COMPLETION_QUEUE_SHUTDOWN) break;
  }
  grpc_completion_queue_destroy(cq);
}

GRPC_completion_queue_operation_status GRPC_commit_ops_and_wait_deadline(GRPC_completion_queue *cq,
                                                                         gpr_timespec deadline,
                                                                         void **tag, bool *ok) {
  for (;;) {
    grpc_call_op_set *set = NULL;
    grpc_event ev = grpc_completion_queue_next(cq, deadline, NULL);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        return GRPC_COMPLETION_QUEUE_TIMEOUT;
      case GRPC_QUEUE_SHUTDOWN:
        return GRPC_COMPLETION_QUEUE_SHUTDOWN;
      case GRPC_OP_COMPLETE:
        set = (grpc_call_op_set *) ev.tag;
        GPR_ASSERT(set != NULL);
        GPR_ASSERT(set->context != NULL);
        // run post-processing for async operations
        bool status = grpc_finish_op_from_call_set(set, set->context);

        if (set->hide_from_user) {
          // don't touch user supplied pointers
          continue;
        }

        *tag = set->user_tag;
        *ok = (ev.success != 0) && status;
        return GRPC_COMPLETION_QUEUE_GOT_EVENT;
    }
  }
}

GRPC_completion_queue_operation_status GRPC_commit_ops_and_wait(GRPC_completion_queue *cq, void **tag, bool *ok) {
  return GRPC_commit_ops_and_wait_deadline(cq, gpr_inf_future(GPR_CLOCK_REALTIME), tag, ok);
}

bool GRPC_completion_queue_pluck_internal(GRPC_completion_queue *cq, void *tag) {
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  grpc_event ev = grpc_completion_queue_pluck(cq, tag, deadline, NULL);
  grpc_call_op_set *set = (grpc_call_op_set *) ev.tag;
  GPR_ASSERT(set != NULL);
  GPR_ASSERT(set->context != NULL);
  GPR_ASSERT(set->user_tag == ev.tag);
  // run post-processing
  grpc_finish_op_from_call_set(set, set->context);
  return ev.success != 0;
}
