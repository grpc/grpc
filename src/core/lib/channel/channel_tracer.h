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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H

#include <grpc/grpc.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

/* Forward declaration */
typedef struct grpc_channel_tracer grpc_channel_tracer;

// #define GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG

/* Creates a new tracer. The caller owns a reference to the returned tracer. */
#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes, intptr_t uuid,
                                                const char* file, int line,
                                                const char* func);
#define GRPC_CHANNEL_TRACER_CREATE(max_nodes, id) \
  grpc_channel_tracer_create(max_nodes, id, __FILE__, __LINE__, __func__)
#else
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes,
                                                intptr_t uuid);
#define GRPC_CHANNEL_TRACER_CREATE(max_nodes, id) \
  grpc_channel_tracer_create(max_nodes, id)
#endif

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer,
                                             const char* file, int line,
                                             const char* func);
void grpc_channel_tracer_unref(grpc_exec_ctx* exec_ctx,
                               grpc_channel_tracer* tracer, const char* file,
                               int line, const char* func);
#define GRPC_CHANNEL_TRACER_REF(tracer) \
  grpc_channel_tracer_ref(tracer, __FILE__, __LINE__, __func__)
#define GRPC_CHANNEL_TRACER_UNREF(exec_ctx, tracer) \
  grpc_channel_tracer_unref(exec_ctx, tracer, __FILE__, __LINE__, __func__)
#else
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer);
void grpc_channel_tracer_unref(grpc_exec_ctx* exec_ctx,
                               grpc_channel_tracer* tracer);
#define GRPC_CHANNEL_TRACER_REF(tracer) grpc_channel_tracer_ref(tracer)
#define GRPC_CHANNEL_TRACER_UNREF(exec_ctx, tracer) \
  grpc_channel_tracer_unref(exec_ctx, tracer)
#endif

/* Adds a new trace node to the tracing object */
void grpc_channel_tracer_add_trace(grpc_exec_ctx* exec_ctx,
                                   grpc_channel_tracer* tracer, grpc_slice data,
                                   grpc_error* error,
                                   grpc_connectivity_state connectivity_state,
                                   grpc_channel_tracer* subchannel);

/* Returns the tracing data rendered as a grpc json string.
   The string is owned by the caller and must be freed. If recursive
   is true, then the string will include the recursive trace for all
   subtracing objects. */
char* grpc_channel_tracer_render_trace(grpc_channel_tracer* tracer,
                                       bool recursive);
/* util functions that perform the uuid -> tracer step for you, and then
   returns the trace for the uuid given. */
char* grpc_channel_tracer_get_trace(intptr_t uuid, bool recursive);

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H */
