/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/iomgr/call_combiner.h"

#include <grpc/support/log.h>

void grpc_call_combiner_init(grpc_call_combiner* call_combiner) {
  gpr_mpscq_init(&call_combiner->queue);
}

void grpc_call_combiner_destroy(grpc_call_combiner* call_combiner) {
  gpr_mpscq_destroy(&call_combiner->queue);
}

void grpc_call_combiner_start(grpc_exec_ctx* exec_ctx,
                              grpc_call_combiner* call_combiner,
                              grpc_closure *closure, grpc_error* error) {
gpr_log(GPR_INFO, "==> grpc_call_combiner_start() [%p] closure=%p", call_combiner, closure);
  size_t prev_size =
      (size_t)gpr_atm_full_fetch_add(&call_combiner->size, (gpr_atm)1);
gpr_log(GPR_INFO, "  size: %zu -> %zu", prev_size, prev_size + 1);
  if (prev_size == 0) {
gpr_log(GPR_INFO, "  EXECUTING IMMEDIATELY");
    // Queue was empty, so execute this closure immediately.
    GRPC_CLOSURE_SCHED(exec_ctx, closure, error);
  } else {
gpr_log(GPR_INFO, "  QUEUING");
    // Queue was not empty, so add closure to queue.
    closure->error_data.error = error;
    gpr_mpscq_push(&call_combiner->queue, (gpr_mpscq_node*)closure);
  }
}

void grpc_call_combiner_stop(grpc_exec_ctx* exec_ctx,
                             grpc_call_combiner* call_combiner) {
gpr_log(GPR_INFO, "==> grpc_call_combiner_stop() [%p]", call_combiner);
  size_t prev_size =
      (size_t)gpr_atm_full_fetch_add(&call_combiner->size, (gpr_atm)-1);
gpr_log(GPR_INFO, "  size: %zu -> %zu", prev_size, prev_size - 1);
  GPR_ASSERT(prev_size >= 1);
  if (prev_size > (gpr_atm)1) {
    while (true) {
gpr_log(GPR_INFO, "  checking queue");
      bool empty;
      grpc_closure* closure = (grpc_closure*)gpr_mpscq_pop_and_check_end(
          &call_combiner->queue, &empty);
      if (closure == NULL) {
        if (!empty) continue;  // Try again.
gpr_log(GPR_INFO, "  queue empty");
      } else {
gpr_log(GPR_INFO, "  EXECUTING FROM QUEUE: closure=%p", closure);
        GRPC_CLOSURE_SCHED(exec_ctx, closure, closure->error_data.error);
      }
      break;
    }
  }
else { gpr_log(GPR_INFO, "  queue empty"); }
}

void grpc_call_combiner_set_notify_on_cancel(grpc_call_combiner* call_combiner,
                                             grpc_closure* closure) {
  call_combiner->notify_on_cancel = closure;
}

void grpc_call_combiner_cancel(grpc_exec_ctx* exec_ctx,
                               grpc_call_combiner* call_combiner,
                               grpc_error* error) {
  if (call_combiner->notify_on_cancel != NULL) {
gpr_log(GPR_INFO, "scheduling notify_on_cancel callback=%p", call_combiner->notify_on_cancel);
    GRPC_CLOSURE_SCHED(exec_ctx, call_combiner->notify_on_cancel,
                       GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}
