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

#include "src/core/lib/channel/channel_tracer.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include <stdlib.h>
#include <string.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/connectivity_state.h"

// One node of tracing data
struct grpc_trace_node {
  const char* file;
  int line;
  const char* data;
  grpc_error* error;
  gpr_timespec time;
  grpc_connectivity_state connectivity_state;
  grpc_trace_node* next;

  // The tracer object that owns this trace node. This is used to ref and
  // unref the tracing object as nodes are added or overwritten
  grpc_channel_tracer* subchannel;
};

struct grpc_trace_node_list {
  uint32_t size;
  uint32_t max_size;
  grpc_trace_node* head_trace;
  grpc_trace_node* tail_trace;
};

/* the channel tracing object */
struct grpc_channel_tracer {
  gpr_refcount refs;
  gpr_mu tracer_mu;
  uint32_t num_nodes_logged;
  grpc_trace_node_list node_list;
  gpr_timespec time_created;
};

grpc_channel_tracer* grpc_channel_tracer_create(uint32_t max_nodes) {
  grpc_channel_tracer* tracer = gpr_zalloc(sizeof(grpc_channel_tracer));
  gpr_mu_init(&tracer->tracer_mu);
  gpr_ref_init(&tracer->refs, 1);
  tracer->node_list.max_size = max_nodes;
  tracer->time_created = gpr_now(GPR_CLOCK_REALTIME);
  return tracer;
}

grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer) {
  gpr_ref(&tracer->refs);
  return tracer;
}

void grpc_channel_tracer_unref(grpc_channel_tracer* tracer) {
  if (!gpr_unref(&tracer->refs)) {
    grpc_channel_tracer_destroy(tracer);
  }
}

void grpc_channel_tracer_destroy(grpc_channel_tracer* tracer) {
  if (!tracer) return;
  // free the nodes. Do they own data?
  // free the subchannel tracers
  // free this tracer
}

static void free_node(grpc_trace_node* node) {
  // no need to free string, since they are always static
  GRPC_ERROR_UNREF(node->error);
  if (node->subchannel) {
    grpc_channel_tracer_unref(node->subchannel);
  }
}

static void add_trace(grpc_trace_node_list* list, const char* file, int line,
                      const char* trace, grpc_error* error,
                      grpc_connectivity_state connectivity_state,
                      grpc_channel_tracer* subchannel) {
  grpc_trace_node* new_trace_node = gpr_malloc(sizeof(grpc_trace_node));
  new_trace_node->file = file;
  new_trace_node->line = line;
  new_trace_node->data = trace;
  new_trace_node->error = error;
  new_trace_node->time = gpr_now(GPR_CLOCK_REALTIME);
  new_trace_node->connectivity_state = connectivity_state;
  new_trace_node->next = NULL;
  new_trace_node->subchannel = subchannel;

  if (subchannel) {
    grpc_channel_tracer_ref(subchannel);
  }

  // first node in case
  if (!list->head_trace) {
    list->head_trace = list->tail_trace = new_trace_node;
  }
  // regular node add case
  else {
    list->tail_trace->next = new_trace_node;
    list->tail_trace = list->tail_trace->next;
  }
  list->size++;

  // maybe garbage collect the end
  if (list->size > list->max_size) {
    grpc_trace_node* to_free = list->head_trace;
    list->head_trace = list->head_trace->next;
    free_node(to_free);
    list->size--;
  }
}

void grpc_channel_tracer_add_trace(const char* file, int line,
                                   grpc_channel_tracer* tracer,
                                   const char* trace, grpc_error* error,
                                   grpc_connectivity_state connectivity_state,
                                   grpc_channel_tracer* subchannel) {
  if (!tracer) return;
  tracer->num_nodes_logged++;
  add_trace(&tracer->node_list, file, line, trace, error, connectivity_state,
            subchannel);
}

static void log_trace(grpc_trace_node* tn, bool is_subchannel) {
  char* maybe_tab = is_subchannel ? "\t\t" : "";
  while (tn) {
    gpr_log(GPR_ERROR, "%s%s:%d - %s, %s, %lld:%d, %s", maybe_tab, tn->file,
            tn->line, tn->data, grpc_error_string(tn->error), tn->time.tv_sec,
            tn->time.tv_nsec,
            grpc_connectivity_state_name(tn->connectivity_state));
    tn = tn->next;
  }
}

void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer) {
  if (!tracer) return;
  gpr_log(GPR_ERROR, "Parent trace:");
  log_trace(tracer->node_list.head_trace, false);
  // while (subchannel_iterator) {
  //   gpr_log(GPR_ERROR, "Child trace:");
  //   log_trace(subchannel_iterator->node_list.head_trace, true);
  //   subchannel_iterator = subchannel_iterator->next;
  // }
}
