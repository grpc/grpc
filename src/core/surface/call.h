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

#ifndef GRPC_INTERNAL_CORE_SURFACE_CALL_H
#define GRPC_INTERNAL_CORE_SURFACE_CALL_H

#include "src/core/channel/channel_stack.h"
#include "src/core/channel/context.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/surface_trace.h"
#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*grpc_ioreq_completion_func)(grpc_exec_ctx *exec_ctx,
                                           grpc_call *call, int success,
                                           void *user_data);

grpc_call *grpc_call_create(grpc_channel *channel, grpc_call *parent_call,
                            gpr_uint32 propagation_mask,
                            grpc_completion_queue *cq,
                            const void *server_transport_data,
                            grpc_mdelem **add_initial_metadata,
                            size_t add_initial_metadata_count,
                            gpr_timespec send_deadline);

void grpc_call_set_completion_queue(grpc_exec_ctx *exec_ctx, grpc_call *call,
                                    grpc_completion_queue *cq);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_call_internal_ref(grpc_call *call, const char *reason);
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *call,
                              const char *reason);
#define GRPC_CALL_INTERNAL_REF(call, reason) \
  grpc_call_internal_ref(call, reason)
#define GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, reason) \
  grpc_call_internal_unref(exec_ctx, call, reason)
#else
void grpc_call_internal_ref(grpc_call *call);
void grpc_call_internal_unref(grpc_exec_ctx *exec_ctx, grpc_call *call);
#define GRPC_CALL_INTERNAL_REF(call, reason) grpc_call_internal_ref(call)
#define GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, reason) \
  grpc_call_internal_unref(exec_ctx, call)
#endif

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call);

grpc_call_error grpc_call_start_batch_and_execute(grpc_exec_ctx *exec_ctx,
                                                  grpc_call *call,
                                                  const grpc_op *ops,
                                                  size_t nops,
                                                  grpc_closure *closure);

/* Given the top call_element, get the call object. */
grpc_call *grpc_call_from_top_element(grpc_call_element *surface_element);

void grpc_call_log_batch(char *file, int line, gpr_log_severity severity,
                         grpc_call *call, const grpc_op *ops, size_t nops,
                         void *tag);

/* Set a context pointer.
   No thread safety guarantees are made wrt this value. */
void grpc_call_context_set(grpc_call *call, grpc_context_index elem,
                           void *value, void (*destroy)(void *value));
/* Get a context pointer. */
void *grpc_call_context_get(grpc_call *call, grpc_context_index elem);

#define GRPC_CALL_LOG_BATCH(sev, call, ops, nops, tag) \
  if (grpc_api_trace) grpc_call_log_batch(sev, call, ops, nops, tag)

gpr_uint8 grpc_call_is_client(grpc_call *call);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_INTERNAL_CORE_SURFACE_CALL_H */
