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
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/connectivity_state.h"

// One node of tracing data
struct grpc_trace_node {
  grpc_slice data;
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

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes,
                                                const char* file, int line,
                                                const char* func) {
#else
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes) {
#endif
  grpc_channel_tracer* tracer = gpr_zalloc(sizeof(grpc_channel_tracer));
  gpr_mu_init(&tracer->tracer_mu);
  gpr_ref_init(&tracer->refs, 1);
#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "%p create [%s:%d %s]", tracer, file, line, func);
#endif
  tracer->node_list.max_size = max_nodes;
  tracer->time_created = gpr_now(GPR_CLOCK_REALTIME);
  return tracer;
}

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer,
                                             const char* file, int line,
                                             const char* func) {
  if (!tracer) return tracer;
  gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d %s]", tracer,
          gpr_atm_no_barrier_load(&tracer->refs.count),
          gpr_atm_no_barrier_load(&tracer->refs.count) + 1, file, line, func);
  gpr_ref(&tracer->refs);
  return tracer;
}
#else
grpc_channel_tracer* grpc_channel_tracer_ref(grpc_channel_tracer* tracer) {
  if (!tracer) return tracer;
  gpr_ref(&tracer->refs);
  return tracer;
}
#endif

static void free_node(grpc_trace_node* node) {
  // no need to free string, since they are always static
  GRPC_ERROR_UNREF(node->error);
  GRPC_CHANNEL_TRACER_UNREF(node->subchannel);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_unref_internal(&exec_ctx, node->data);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(node);
}

void grpc_channel_tracer_destroy(grpc_channel_tracer* tracer) {
  grpc_trace_node* it = tracer->node_list.head_trace;
  while (it) {
    grpc_trace_node* to_free = it;
    it = it->next;
    free_node(to_free);
  }
  gpr_free(tracer);
}

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
void grpc_channel_tracer_unref(grpc_channel_tracer* tracer, const char* file,
                               int line, const char* func) {
  if (!tracer) return;
  gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d %s]", tracer,
          gpr_atm_no_barrier_load(&tracer->refs.count),
          gpr_atm_no_barrier_load(&tracer->refs.count) - 1, file, line, func);
  if (gpr_unref(&tracer->refs)) {
    grpc_channel_tracer_destroy(tracer);
  }
}
#else
void grpc_channel_tracer_unref(grpc_channel_tracer* tracer) {
  if (!tracer) return;
  if (gpr_unref(&tracer->refs)) {
    grpc_channel_tracer_destroy(tracer);
  }
}
#endif

static void add_trace(grpc_trace_node_list* list, grpc_slice trace,
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

  GRPC_CHANNEL_TRACER_REF(subchannel);

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
                                   grpc_slice trace, grpc_error* error,
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
  child->owns_value = owns_value;
  child->parent = parent;
  child->value = value;
  child->key = key;
  return child;
}

static void populate_node_data(grpc_trace_node* node, grpc_json* json,
                               bool is_parent) {
  grpc_json* child = NULL;
  child = create_child(child, json, "data", grpc_slice_to_c_string(node->data),
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
  if (is_parent && node->subchannel) {
    char* subchannel_addr_str;
    gpr_asprintf(&subchannel_addr_str, "%p", node->subchannel);
    child = create_child(child, json, "subchannelId", subchannel_addr_str,
                         GRPC_JSON_NUMBER, true);
  }
}

static void populate_node_list_data(grpc_trace_node_list* list, grpc_json* json,
                                    bool is_parent) {
  grpc_json* child = NULL;
  grpc_trace_node* it = list->head_trace;
  while (it) {
    child = create_child(child, json, NULL, NULL, GRPC_JSON_OBJECT, false);
    populate_node_data(it, child, is_parent);
    it = it->next;
  }
}

static void populate_channel_data(grpc_channel_tracer* tracer, grpc_json* json,
                                  bool is_parent) {
  grpc_json* child = NULL;

  if (!is_parent) {
    char* subchannel_addr_str;
    gpr_asprintf(&subchannel_addr_str, "%p", tracer);
    child = create_child(child, json, "subchannelId", subchannel_addr_str,
                         GRPC_JSON_NUMBER, true);
  }

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
  populate_node_list_data(&tracer->node_list, child, is_parent);
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
      gpr_zalloc(sizeof(grpc_channel_tracer*) * list->max_size);
  size_t insert = 0;

  grpc_trace_node* it = list->head_trace;
  while (it) {
    if (it->subchannel && !check_exists(it->subchannel, seen, list->max_size)) {
      seen[insert++] = it->subchannel;
      child = create_child(child, json, NULL, NULL, GRPC_JSON_OBJECT, false);
      populate_channel_data(it->subchannel, child, false);
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
  populate_channel_data(tracer, child, true);
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
