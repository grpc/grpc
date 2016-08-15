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

/**
 * Wraps the grpc_completion_queue type.
 */

#include "src/c/completion_queue.h"
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc_c/grpc_c.h>
#include "src/c/call_ops.h"
#include "src/c/init_shutdown.h"

GRPC_completion_queue *GRPC_completion_queue_create() {
  GRPC_ensure_grpc_init();
  return grpc_completion_queue_create(NULL);
}

void GRPC_completion_queue_shutdown(GRPC_completion_queue *cq) {
  grpc_completion_queue_shutdown(cq);
}

void GRPC_completion_queue_destroy(GRPC_completion_queue *cq) {
  grpc_completion_queue_destroy(cq);
}

void GRPC_completion_queue_shutdown_wait(GRPC_completion_queue *cq) {
  for (;;) {
    void *tag;
    bool ok;
    if (GRPC_completion_queue_next(cq, &tag, &ok) ==
        GRPC_COMPLETION_QUEUE_SHUTDOWN)
      break;
  }
}

GRPC_completion_queue_operation_status GRPC_completion_queue_next_deadline(
    GRPC_completion_queue *cq, GRPC_timespec deadline, void **tag, bool *ok) {
  for (;;) {
    GRPC_call_op_set *set = NULL;
    grpc_event ev = grpc_completion_queue_next(cq, deadline, NULL);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        return GRPC_COMPLETION_QUEUE_TIMEOUT;
      case GRPC_QUEUE_SHUTDOWN:
        return GRPC_COMPLETION_QUEUE_SHUTDOWN;
      case GRPC_OP_COMPLETE:
        set = (GRPC_call_op_set *)ev.tag;
        GPR_ASSERT(set != NULL);
        GPR_ASSERT(set->context != NULL);
        // run post-processing for async operations
        bool status = GRPC_finish_op_from_call_set(set, set->context);
        bool hide_from_user = set->hide_from_user;
        void *user_tag = set->user_tag;

        // run user-defined cleanup
        if (set->async_cleanup.callback) {
          set->async_cleanup.callback(set->async_cleanup.arg);
        }
        // set could be freed from this point onwards

        if (hide_from_user) {
          // don't touch user supplied pointers
          continue;
        }

        *tag = user_tag;
        *ok = (ev.success != 0) && status;

        return GRPC_COMPLETION_QUEUE_GOT_EVENT;
    }
  }
}

GRPC_completion_queue_operation_status GRPC_completion_queue_next(
    GRPC_completion_queue *cq, void **tag, bool *ok) {
  return GRPC_completion_queue_next_deadline(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), tag, ok);
}

bool GRPC_completion_queue_pluck_internal(GRPC_completion_queue *cq,
                                          void *tag) {
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  grpc_event ev = grpc_completion_queue_pluck(cq, tag, deadline, NULL);
  GRPC_call_op_set *set = (GRPC_call_op_set *)ev.tag;
  GPR_ASSERT(set != NULL);
  GPR_ASSERT(set->context != NULL);
  GPR_ASSERT(set->user_tag == ev.tag);
  // run post-processing
  bool status = GRPC_finish_op_from_call_set(set, set->context);
  // run user-defined cleanup
  if (set->async_cleanup.callback) {
    set->async_cleanup.callback(set->async_cleanup.arg);
  }
  return (ev.success != 0) && status;
}
