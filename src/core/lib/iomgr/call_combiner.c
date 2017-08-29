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

grpc_tracer_flag grpc_call_combiner_trace =
    GRPC_TRACER_INITIALIZER(false, "call_combiner");

static grpc_error* decode_cancel_state_error(gpr_atm cancel_state) {
  if (cancel_state & 1) {
    return (grpc_error*)(cancel_state & ~(gpr_atm)1);
  }
  return GRPC_ERROR_NONE;
}

static gpr_atm encode_cancel_state_error(grpc_error* error) {
  return (gpr_atm)1 | (gpr_atm)error;
}

void grpc_call_combiner_init(grpc_call_combiner* call_combiner) {
  gpr_mpscq_init(&call_combiner->queue);
}

void grpc_call_combiner_destroy(grpc_call_combiner* call_combiner) {
  gpr_mpscq_destroy(&call_combiner->queue);
  GRPC_ERROR_UNREF(decode_cancel_state_error(call_combiner->cancel_state));
}

#ifndef NDEBUG
#define DEBUG_ARGS , const char *file, int line
#define DEBUG_FMT_STR "%s:%d: "
#define DEBUG_FMT_ARGS , file, line
#else
#define DEBUG_ARGS
#define DEBUG_FMT_STR
#define DEBUG_FMT_ARGS
#endif

void grpc_call_combiner_start(grpc_exec_ctx* exec_ctx,
                              grpc_call_combiner* call_combiner,
                              grpc_closure* closure,
                              grpc_error* error DEBUG_ARGS,
                              const char* reason) {
  if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
    gpr_log(GPR_DEBUG,
            "==> grpc_call_combiner_start() [%p] closure=%p [" DEBUG_FMT_STR
            "%s] error=%s",
            call_combiner, closure DEBUG_FMT_ARGS, reason,
            grpc_error_string(error));
  }
  size_t prev_size =
      (size_t)gpr_atm_full_fetch_add(&call_combiner->size, (gpr_atm)1);
  if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
    gpr_log(GPR_DEBUG, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
            prev_size + 1);
  }
  if (prev_size == 0) {
    if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
      gpr_log(GPR_DEBUG, "  EXECUTING IMMEDIATELY");
    }
    // Queue was empty, so execute this closure immediately.
    GRPC_CLOSURE_SCHED(exec_ctx, closure, error);
  } else {
    if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
      gpr_log(GPR_INFO, "  QUEUING");
    }
    // Queue was not empty, so add closure to queue.
    closure->error_data.error = error;
    gpr_mpscq_push(&call_combiner->queue, (gpr_mpscq_node*)closure);
  }
}

void grpc_call_combiner_stop(grpc_exec_ctx* exec_ctx,
                             grpc_call_combiner* call_combiner DEBUG_ARGS,
                             const char* reason) {
  if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
    gpr_log(GPR_DEBUG,
            "==> grpc_call_combiner_stop() [%p] [" DEBUG_FMT_STR "%s]",
            call_combiner DEBUG_FMT_ARGS, reason);
  }
  size_t prev_size =
      (size_t)gpr_atm_full_fetch_add(&call_combiner->size, (gpr_atm)-1);
  if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
    gpr_log(GPR_DEBUG, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
            prev_size - 1);
  }
  GPR_ASSERT(prev_size >= 1);
  if (prev_size > 1) {
    while (true) {
      if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
        gpr_log(GPR_DEBUG, "  checking queue");
      }
      bool empty;
      grpc_closure* closure = (grpc_closure*)gpr_mpscq_pop_and_check_end(
          &call_combiner->queue, &empty);
      if (closure == NULL) {
        // This can happen either due to a race condition within the mpscq
        // code or because of a race with grpc_call_combiner_start().
        if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
          gpr_log(GPR_DEBUG, "  queue returned no result; checking again");
        }
        continue;
      }
      if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
        gpr_log(GPR_DEBUG, "  EXECUTING FROM QUEUE: closure=%p error=%s",
                closure, grpc_error_string(closure->error_data.error));
      }
      GRPC_CLOSURE_SCHED(exec_ctx, closure, closure->error_data.error);
      break;
    }
  } else if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
    gpr_log(GPR_DEBUG, "  queue empty");
  }
}

void grpc_call_combiner_set_notify_on_cancel(grpc_exec_ctx* exec_ctx,
                                             grpc_call_combiner* call_combiner,
                                             grpc_closure* closure) {
  while (true) {
    // Decode original state.
    gpr_atm original_state = gpr_atm_acq_load(&call_combiner->cancel_state);
    grpc_error* original_error = decode_cancel_state_error(original_state);
    // If error is set, invoke the cancellation closure immediately.
    // Otherwise, store the new closure.
    if (original_error != GRPC_ERROR_NONE) {
      if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
        gpr_log(GPR_DEBUG,
                "call_combiner=%p: scheduling notify_on_cancel callback=%p "
                "for pre-existing cancellation",
                call_combiner, closure);
      }
      GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_REF(original_error));
      break;
    } else {
      if (gpr_atm_full_cas(&call_combiner->cancel_state, original_state,
                           (gpr_atm)closure)) {
        if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
          gpr_log(GPR_DEBUG, "call_combiner=%p: setting notify_on_cancel=%p",
                  call_combiner, closure);
        }
        // If we replaced an earlier closure, invoke the original
        // closure with GRPC_ERROR_NONE.  This allows callers to clean
        // up any resources they may be holding for the callback.
        if (original_state != 0) {
          closure = (grpc_closure*)original_state;
          if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
            gpr_log(GPR_DEBUG,
                    "call_combiner=%p: scheduling old cancel callback=%p",
                    call_combiner, closure);
          }
          GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_NONE);
        }
        break;
      }
    }
    // cas failed, try again.
  }
}

void grpc_call_combiner_cancel(grpc_exec_ctx* exec_ctx,
                               grpc_call_combiner* call_combiner,
                               grpc_error* error) {
  while (true) {
    gpr_atm original_state = gpr_atm_acq_load(&call_combiner->cancel_state);
    grpc_error* original_error = decode_cancel_state_error(original_state);
    if (original_error != GRPC_ERROR_NONE) {
      GRPC_ERROR_UNREF(error);
      break;
    }
    if (gpr_atm_full_cas(&call_combiner->cancel_state, original_state,
                         encode_cancel_state_error(error))) {
      if (original_state != 0) {
        grpc_closure* notify_on_cancel = (grpc_closure*)original_state;
        if (GRPC_TRACER_ON(grpc_call_combiner_trace)) {
          gpr_log(GPR_DEBUG,
                  "call_combiner=%p: scheduling notify_on_cancel callback=%p",
                  call_combiner, notify_on_cancel);
        }
        GRPC_CLOSURE_SCHED(exec_ctx, notify_on_cancel, GRPC_ERROR_REF(error));
      }
      break;
    }
    // cas failed, try again.
  }
}
