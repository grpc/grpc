/*
 *
 * Copyright 2015-2016, Google Inc.
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

#ifndef GRPC_CORE_LIB_SURFACE_COMPLETION_QUEUE_H
#define GRPC_CORE_LIB_SURFACE_COMPLETION_QUEUE_H

/* Internal API for completion queues */

#include <grpc/grpc.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/pollset.h"

/* These trace flags default to 1. The corresponding lines are only traced
   if grpc_api_trace is also truthy */
extern grpc_tracer_flag grpc_cq_pluck_trace;
extern grpc_tracer_flag grpc_cq_event_timeout_trace;
extern grpc_tracer_flag grpc_trace_operation_failures;
#ifndef NDEBUG
extern grpc_tracer_flag grpc_trace_pending_tags;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_cq_completion {
  gpr_mpscq_node node;

  /** user supplied tag */
  void *tag;
  /** done callback - called when this queue element is no longer
      needed by the completion queue */
  void (*done)(grpc_exec_ctx *exec_ctx, void *done_arg,
               struct grpc_cq_completion *c);
  void *done_arg;
  /** next pointer; low bit is used to indicate success or not */
  uintptr_t next;
} grpc_cq_completion;

//#define GRPC_CQ_REF_COUNT_DEBUG

#ifdef GRPC_CQ_REF_COUNT_DEBUG
void grpc_cq_internal_ref(grpc_completion_queue *cc, const char *reason,
                          const char *file, int line);
void grpc_cq_internal_unref(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cc,
                            const char *reason, const char *file, int line);
#define GRPC_CQ_INTERNAL_REF(cc, reason) \
  grpc_cq_internal_ref(cc, reason, __FILE__, __LINE__)
#define GRPC_CQ_INTERNAL_UNREF(ec, cc, reason) \
  grpc_cq_internal_unref(ec, cc, reason, __FILE__, __LINE__)
#else
void grpc_cq_internal_ref(grpc_completion_queue *cc);
void grpc_cq_internal_unref(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cc);
#define GRPC_CQ_INTERNAL_REF(cc, reason) grpc_cq_internal_ref(cc)
#define GRPC_CQ_INTERNAL_UNREF(ec, cc, reason) grpc_cq_internal_unref(ec, cc)
#endif

/* Flag that an operation is beginning: the completion channel will not finish
   shutdown until a corrensponding grpc_cq_end_* call is made.
   \a tag is currently used only in debug builds. */
void grpc_cq_begin_op(grpc_completion_queue *cc, void *tag);

/* Queue a GRPC_OP_COMPLETED operation; tag must correspond to the tag passed to
   grpc_cq_begin_op */
void grpc_cq_end_op(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cc,
                    void *tag, grpc_error *error,
                    void (*done)(grpc_exec_ctx *exec_ctx, void *done_arg,
                                 grpc_cq_completion *storage),
                    void *done_arg, grpc_cq_completion *storage);

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc);
grpc_completion_queue *grpc_cq_from_pollset(grpc_pollset *ps);

void grpc_cq_mark_server_cq(grpc_completion_queue *cc);
bool grpc_cq_is_server_cq(grpc_completion_queue *cc);
bool grpc_cq_can_listen(grpc_completion_queue *cc);

grpc_cq_completion_type grpc_get_cq_completion_type(grpc_completion_queue *cc);

int grpc_get_cq_poll_num(grpc_completion_queue *cc);

grpc_completion_queue *grpc_completion_queue_create_internal(
    grpc_cq_completion_type completion_type, grpc_cq_polling_type polling_type);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SURFACE_COMPLETION_QUEUE_H */
