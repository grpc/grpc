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
  const char* data;
  grpc_error* error;
  gpr_timespec time_created;
  grpc_connectivity_state connectivity_state;
  grpc_trace_node* next;

  // The tracer object that owns this trace node. This is used to ref and
  // unref the tracing object as nodes are added or overwritten
  grpc_channel_tracer* subchannel;
};

struct grpc_trace_node_list {
  size_t size;
  size_t max_size;
  grpc_trace_node* head_trace;
  grpc_trace_node* tail_trace;
};

/* the channel tracing object */
struct grpc_channel_tracer {
  gpr_refcount refs;
  gpr_mu tracer_mu;
  int64_t num_nodes_logged;
  grpc_trace_node_list node_list;
  gpr_timespec time_created;
};

grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes) {
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
  gpr_free(node);
}

static void add_trace(grpc_trace_node_list* list, const char* trace,
                      grpc_error* error,
                      grpc_connectivity_state connectivity_state,
                      grpc_channel_tracer* subchannel) {
  grpc_trace_node* new_trace_node = gpr_malloc(sizeof(grpc_trace_node));
  new_trace_node->data = trace;
  new_trace_node->error = error;
  new_trace_node->time_created = gpr_now(GPR_CLOCK_REALTIME);
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

void grpc_channel_tracer_add_trace(grpc_channel_tracer* tracer,
                                   const char* trace, grpc_error* error,
                                   grpc_connectivity_state connectivity_state,
                                   grpc_channel_tracer* subchannel) {
  if (!tracer) return;
  tracer->num_nodes_logged++;
  add_trace(&tracer->node_list, trace, error, connectivity_state, subchannel);
}

// TODO(ncteisen): pull this function into a helper location
static grpc_json* link_json(grpc_json* child, grpc_json* brother,
                            grpc_json* parent) {
  if (brother) brother->next = child;
  if (!parent->child) parent->child = child;
  return child;
}

// TODO(ncteisen): pull this function into a helper location
static grpc_json* create_child(grpc_json* brother, grpc_json* parent,
                               const char* key, const char* value,
                               grpc_json_type type, bool owns_value) {
  grpc_json* child = grpc_json_create(type);
  link_json(child, brother, parent);
  child->parent = parent;
  child->value = value;
  child->key = key;
  return child;
}

// static const char* get_tracer_data(grpc_channel_tracer* tracer, bool
// is_parent) {
//   grpc_json *json = grpc_json_create(GRPC_JSON_OBJECT);
//   grpc_json *child = NULL;

//   char num_nodes_logged_str[GPR_INT64TOA_MIN_BUFSIZE];
//   int64_ttoa(tracer->num_nodes_logged, num_nodes_logged_str);
//   child = create_child(NULL, json, "numNodesLogged", num_nodes_logged_str,
//   GRPC_JSON_NUMBER);

//   const char* json_str = grpc_json_dump_to_string(json, 0);
//   grpc_json_destroy(json);
//   return json_str;
// }

static void populate_node_data(grpc_trace_node* node, grpc_json* json) {
  grpc_json* child = NULL;
  child = create_child(child, json, "data", gpr_strdup(node->data),
                       GRPC_JSON_STRING, true);
  child = create_child(child, json, "error",
                       gpr_strdup(grpc_error_string(node->error)),
                       GRPC_JSON_STRING, true);
  char* time_str;
  gpr_asprintf(&time_str, "%" PRId64 ".%09d", node->time_created.tv_sec,
               node->time_created.tv_nsec);
  child = create_child(child, json, "time", time_str, GRPC_JSON_STRING, true);
  child = create_child(child, json, "state",
                       grpc_connectivity_state_name(node->connectivity_state),
                       GRPC_JSON_STRING, false);
}

static void populate_node_list_data(grpc_trace_node_list* list,
                                    grpc_json* json) {
  grpc_json* child = NULL;
  grpc_trace_node* it = list->head_trace;
  while (it) {
    child = create_child(child, json, NULL, NULL, GRPC_JSON_OBJECT, false);
    populate_node_data(it, child);
    it = it->next;
  }
}

static void populate_channel_data(grpc_channel_tracer* tracer,
                                  grpc_json* json) {
  grpc_json* child = NULL;
  char* num_nodes_logged_str;
  gpr_asprintf(&num_nodes_logged_str, "%" PRId64, tracer->num_nodes_logged);
  child = create_child(child, json, "numNodesLogged", num_nodes_logged_str,
                       GRPC_JSON_NUMBER, true);
  char* time_str;
  gpr_asprintf(&time_str, "%" PRId64 ".%09d", tracer->time_created.tv_sec,
               tracer->time_created.tv_nsec);
  child =
      create_child(child, json, "startTime", time_str, GRPC_JSON_STRING, true);
  child = create_child(child, json, "nodes", NULL, GRPC_JSON_ARRAY, false);
  populate_node_list_data(&tracer->node_list, child);
}

static bool check_exists(grpc_channel_tracer* key, grpc_channel_tracer** arr,
                         size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (key == arr[i]) return true;
  }
  return false;
}

static void populate_subchannel_data(grpc_trace_node_list* list,
                                     grpc_json* json) {
  grpc_json* child = NULL;
  grpc_channel_tracer** seen =
      gpr_malloc(sizeof(grpc_channel_tracer*) * list->max_size);
  size_t insert = 0;

  grpc_trace_node* it = list->head_trace;
  while (it) {
    if (it->subchannel && !check_exists(it->subchannel, seen, list->max_size)) {
      seen[insert++] = it->subchannel;
      child = create_child(child, json, NULL, NULL, GRPC_JSON_OBJECT, false);
      populate_channel_data(it->subchannel, child);
    }
    it = it->next;
  }

  gpr_free(seen);
}

grpc_json* grpc_channel_tracer_get_trace(grpc_channel_tracer* tracer) {
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* child = NULL;

  child =
      create_child(child, json, "channelData", NULL, GRPC_JSON_OBJECT, false);
  populate_channel_data(tracer, child);
  child =
      create_child(child, json, "subchannelData", NULL, GRPC_JSON_ARRAY, false);
  populate_subchannel_data(&tracer->node_list, child);

  return json;
}

void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer) {
  grpc_json* json = grpc_channel_tracer_get_trace(tracer);
  const char* json_str = grpc_json_dump_to_string(json, 1);
  grpc_json_destroy(json);
  gpr_log(GPR_DEBUG, "\n%s", json_str);
  gpr_free((void*)json_str);
}
