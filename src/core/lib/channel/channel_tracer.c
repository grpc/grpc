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

#include <stdlib.h>
#include <string.h>
#include "src/core/lib/channel/channel_tracer.h"
#include <grpc/grpc.h>
#include "src/core/lib/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/connectivity_state.h"

struct grpc_trace_node {
  char* data;
  grpc_error* error;
  gpr_timespec time;
  grpc_connectivity_state connectivity_state;
  grpc_trace_node* next;
};

grpc_channel_tracer* grpc_channel_tracer_init_tracer()
{
  grpc_channel_tracer* tracer = gpr_malloc(sizeof(grpc_channel_tracer));
  memset(tracer, 0, sizeof(*tracer));
  gpr_mu_init(&tracer->tracer_mu);
  return tracer;
}

grpc_subchannel_tracer* grpc_subchannel_tracer_init_tracer()
{
  grpc_subchannel_tracer* tracer = gpr_malloc(sizeof(grpc_subchannel_tracer));
  memset(tracer, 0, sizeof(*tracer));
  gpr_mu_init(&tracer->tracer_mu);
  return tracer;
}

void grpc_channel_tracer_destroy_tracer(grpc_channel_tracer* tracer) 
{
  if (!tracer) return;
  // free the nodes. Do they own data?
  // free the subchannel tracers
  // free this tracer
}

void grpc_subchannel_tracer_destroy_tracer(grpc_subchannel_tracer* tracer) 
{
  if (!tracer) return;
  // free the nodes. Do they own data?
  // free the subchannel tracers
  // free this tracer
}

static grpc_trace_node* grpc_channel_tracer_new_node(char* trace, grpc_error* error,
    gpr_timespec time, grpc_connectivity_state connectivity_state) 
{
  grpc_trace_node* new_trace_node = gpr_malloc(sizeof(grpc_trace_node));
  new_trace_node->data = trace;
  new_trace_node->error = error;
  new_trace_node->time = time;
  new_trace_node->connectivity_state = connectivity_state;
  new_trace_node->next = NULL;
  return new_trace_node;
}

void grpc_channel_tracer_add_trace(trace_node_list* node_list, 
    char* trace, grpc_error* error, gpr_timespec time, 
    grpc_connectivity_state connectivity_state)
{
  if (!node_list) return;
  grpc_trace_node* new_trace_node =
      grpc_channel_tracer_new_node(trace, error, time, connectivity_state);
  if (!node_list->head_trace) {
    node_list->head_trace = node_list->tail_trace = new_trace_node;
  } else {
    node_list->tail_trace->next = new_trace_node;
    node_list->tail_trace = node_list->tail_trace->next;
  }
  node_list->size += 1;
}


void grpc_channel_tracer_add_subchannel(grpc_channel_tracer* tracer, grpc_subchannel_tracer* new_subchannel)
{
  if (!tracer) return;
  if (!tracer->head_subchannel) {
    tracer->head_subchannel = tracer->tail_subchannel = new_subchannel;
  } else {
    tracer->tail_subchannel->next = new_subchannel;
    tracer->tail_subchannel = tracer->tail_subchannel->next;
  }

}

static void log_trace(grpc_trace_node* tn, bool is_subchannel)
{
  char* maybe_tab = is_subchannel ? "\t\t" : "";
  while (tn) {
    gpr_log(GPR_ERROR, "%s%s, %s, %lld:%d, %s", maybe_tab, tn->data,
        grpc_error_string(tn->error), tn->time.tv_sec, tn->time.tv_nsec, 
        grpc_connectivity_state_name(tn->connectivity_state));
    tn = tn->next;
  }
}

void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer)
{
  if (!tracer) return;
  grpc_subchannel_tracer* subchannel_iterator = tracer->head_subchannel;
  gpr_log(GPR_ERROR, "Parent trace:");
  log_trace(tracer->node_list.head_trace, false);
  while (subchannel_iterator) {
    gpr_log(GPR_ERROR, "Child trace:");
    log_trace(subchannel_iterator->node_list.head_trace, true);
    subchannel_iterator = subchannel_iterator->next;
  }
}
