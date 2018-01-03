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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H

#include <grpc/grpc.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

/* Forward declaration */
typedef struct grpc_channel_tracer grpc_channel_tracer;

extern grpc_core::DebugOnlyTraceFlag grpc_trace_channel_tracer_refcount;

/* Creates a new tracer. The caller owns a reference to the returned tracer. */
#ifndef NDEBUG
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

#ifndef NDEBUG
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer,
                                             const char* file, int line,
                                             const char* func);
void grpc_channel_tracer_unref(grpc_channel_tracer* tracer, const char* file,
                               int line, const char* func);
#define GRPC_CHANNEL_TRACER_REF(tracer) \
  grpc_channel_tracer_ref(tracer, __FILE__, __LINE__, __func__)
#define GRPC_CHANNEL_TRACER_UNREF(tracer) \
  grpc_channel_tracer_unref(tracer, __FILE__, __LINE__, __func__)
#else
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer);
void grpc_channel_tracer_unref(grpc_channel_tracer* tracer);
#define GRPC_CHANNEL_TRACER_REF(tracer) grpc_channel_tracer_ref(tracer)
#define GRPC_CHANNEL_TRACER_UNREF(tracer) grpc_channel_tracer_unref(tracer)
#endif

/* Adds a new trace node to the tracing object */
void grpc_channel_tracer_add_trace(grpc_channel_tracer* tracer, grpc_slice data,
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
