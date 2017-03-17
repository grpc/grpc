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
typedef struct grpc_subchannel_tracer grpc_subchannel_tracer;
typedef struct grpc_trace_node grpc_trace_node;
typedef struct trace_node_list trace_node_list;

/* Dumps all of the trace to stderr */
void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer);

/* returns the tracing data in the form of a grpc json object */
grpc_json* grpc_channel_tracer_get_trace(grpc_channel_tracer* tracer);

/* Adds a string of trace to the tracing object */
void grpc_channel_tracer_add_trace(trace_node_list* node_list, char* trace,
                                   struct grpc_error* error, gpr_timespec time,
                                   grpc_connectivity_state connectivity_state);

/* Adds a subchannel to the tracing object */
void grpc_channel_tracer_add_subchannel(grpc_channel_tracer* tracer,
                                        grpc_subchannel_tracer* subchannel);

/* Initializes the tracing object with gpr_malloc. The caller has
   ownership over the returned tracing object */
grpc_channel_tracer* grpc_channel_tracer_init_tracer();
grpc_subchannel_tracer* grpc_subchannel_tracer_init_tracer();

/* Frees all of the resources held by the tracer object */
void grpc_channel_tracer_destroy_tracer(grpc_channel_tracer* tracer);
void grpc_subchannel_tracer_destroy_tracer(grpc_subchannel_tracer* tracer);

struct trace_node_list {
  int size;
  grpc_trace_node* head_trace;
  grpc_trace_node* tail_trace;
  grpc_subchannel_tracer* referrenced_subchannel;
};

/* the channel tracing object */
struct grpc_channel_tracer {
  gpr_mu tracer_mu;
  trace_node_list node_list;
  grpc_subchannel_tracer* head_subchannel;
  grpc_subchannel_tracer* tail_subchannel;
};

/* the subchannel tracing object */
struct grpc_subchannel_tracer {
  gpr_mu tracer_mu;
  int refcount;
  trace_node_list node_list;
  grpc_subchannel_tracer* next;
  grpc_subchannel_tracer* prev;
};

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_TRACER_H */
