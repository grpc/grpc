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

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct grpc_channel_tracer grpc_channel_tracer;
typedef struct grpc_trace_node grpc_trace_node;
typedef struct grpc_trace_node_list grpc_trace_node_list;

/* Adds a new trace node to the tracing object */
void grpc_channel_tracer_add_trace(grpc_channel_tracer* tracer,
                                   const char* trace, struct grpc_error* error,
                                   grpc_connectivity_state connectivity_state,
                                   grpc_channel_tracer* subchannel);

// grpc_channel_tracer is refcounted
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer);
void grpc_channel_tracer_unref(grpc_channel_tracer* tracer);

/* Dumps all of the trace to stderr */
void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer);

/* Returns the tracing data in the form of a grpc json string.
   The string is owned by the caller and must be freed. */
grpc_json* grpc_channel_tracer_get_trace(grpc_channel_tracer* tracer);

/* Initializes the tracing object with gpr_malloc. The caller has
   ownership over the returned tracing object */
grpc_channel_tracer* grpc_channel_tracer_create();

/* Frees all of the resources held by the tracer object */
void grpc_channel_tracer_destroy(grpc_channel_tracer* tracer);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H */
