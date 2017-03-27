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

typedef struct grpc_trace_node grpc_trace_node;

// One node of tracing data
struct grpc_trace_node {
  grpc_slice data;
  grpc_error* error;
  gpr_timespec time_created;
  grpc_connectivity_state connectivity_state;
  grpc_trace_node* next;

  // the tracer object for the (sub)channel that this trace node refers to.
  grpc_channel_tracer* referenced_tracer;
};

/* the channel tracing object */
struct grpc_channel_tracer {
  gpr_refcount refs;
  gpr_mu tracer_mu;
  const char* id;
  uint64_t num_nodes_logged;
  size_t list_size;
  size_t max_list_size;
  grpc_trace_node* head_trace;
  grpc_trace_node* tail_trace;
  gpr_timespec time_created;
};

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes,
                                                const char* id,
                                                const char* file, int line,
                                                const char* func) {
#else
grpc_channel_tracer* grpc_channel_tracer_create(size_t max_nodes,
                                                const char* id) {
#endif
  grpc_channel_tracer* tracer = gpr_zalloc(sizeof(grpc_channel_tracer));
  gpr_mu_init(&tracer->tracer_mu);
  gpr_ref_init(&tracer->refs, 1);
#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "%p create [%s:%d %s]", tracer, file, line, func);
#endif
  tracer->id = gpr_strdup(id);
  tracer->max_list_size = max_nodes;
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

static void free_node(grpc_exec_ctx* exec_ctx, grpc_trace_node* node) {
  GRPC_ERROR_UNREF(node->error);
  GRPC_CHANNEL_TRACER_UNREF(exec_ctx, node->referenced_tracer);
  grpc_slice_unref_internal(exec_ctx, node->data);
  gpr_free(node);
}

static void grpc_channel_tracer_destroy(grpc_exec_ctx* exec_ctx,
                                        grpc_channel_tracer* tracer) {
  grpc_trace_node* it = tracer->head_trace;
  while (it != NULL) {
    grpc_trace_node* to_free = it;
    it = it->next;
    free_node(exec_ctx, to_free);
  }
  gpr_free((void*)tracer->id);
  gpr_mu_destroy(&tracer->tracer_mu);
  gpr_free(tracer);
}

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
void grpc_channel_tracer_unref(grpc_exec_ctx* exec_ctx,
                               grpc_channel_tracer* tracer, const char* file,
                               int line, const char* func) {
  if (!tracer) return;
  gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d %s]", tracer,
          gpr_atm_no_barrier_load(&tracer->refs.count),
          gpr_atm_no_barrier_load(&tracer->refs.count) - 1, file, line, func);
  if (gpr_unref(&tracer->refs)) {
    grpc_channel_tracer_destroy(exec_ctx, tracer);
  }
}
#else
void grpc_channel_tracer_unref(grpc_exec_ctx* exec_ctx,
                               grpc_channel_tracer* tracer) {
  if (!tracer) return;
  if (gpr_unref(&tracer->refs)) {
    grpc_channel_tracer_destroy(exec_ctx, tracer);
  }
}
#endif

void grpc_channel_tracer_add_trace(grpc_exec_ctx* exec_ctx,
                                   grpc_channel_tracer* tracer, grpc_slice data,
                                   grpc_error* error,
                                   grpc_connectivity_state connectivity_state,
                                   grpc_channel_tracer* referenced_tracer) {
  if (!tracer) return;
  ++tracer->num_nodes_logged;
  // create and fill up the new node
  grpc_trace_node* new_trace_node = gpr_malloc(sizeof(grpc_trace_node));
  new_trace_node->data = data;
  new_trace_node->error = error;
  new_trace_node->time_created = gpr_now(GPR_CLOCK_REALTIME);
  new_trace_node->connectivity_state = connectivity_state;
  new_trace_node->next = NULL;
  new_trace_node->referenced_tracer =
      GRPC_CHANNEL_TRACER_REF(referenced_tracer);
  // first node case
  if (tracer->head_trace == NULL) {
    tracer->head_trace = tracer->tail_trace = new_trace_node;
  }
  // regular node add case
  else {
    tracer->tail_trace->next = new_trace_node;
    tracer->tail_trace = tracer->tail_trace->next;
  }
  ++tracer->list_size;
  // maybe garbage collect the end
  if (tracer->list_size > tracer->max_list_size) {
    grpc_trace_node* to_free = tracer->head_trace;
    tracer->head_trace = tracer->head_trace->next;
    free_node(exec_ctx, to_free);
    --tracer->list_size;
  }
}

static char* fmt_time(gpr_timespec tm) {
  char buffer[100];
  struct tm* tm_info;
  tm_info = localtime((const time_t*)&tm.tv_sec);
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  char* full_time_str;
  gpr_asprintf(&full_time_str, "%s.%09dZ", buffer, tm.tv_nsec);
  return full_time_str;
}

typedef struct tracer_tracker tracer_tracker;
struct tracer_tracker {
  grpc_channel_tracer** tracers;
  size_t size;
  size_t cap;
};

static void track_tracer(tracer_tracker* tracker, grpc_channel_tracer* tracer) {
  if (tracker->size >= tracker->cap) {
    tracker->cap = GPR_MAX(5, 3 * tracker->cap / 2);
    tracker->tracers = gpr_realloc(tracker->tracers, tracker->cap);
  }
  tracker->tracers[tracker->size++] = tracer;
}

static bool check_tracer_tracked(tracer_tracker* tracker,
                                 grpc_channel_tracer* tracer) {
  for (size_t i = 0; i < tracker->size; ++i) {
    if (tracker->tracers[i] == tracer) return true;
  }
  return false;
}

static grpc_json* create_child_find_brother(grpc_json* parent, const char* key,
                                            const char* value,
                                            grpc_json_type type,
                                            bool owns_value) {
  grpc_json* child = parent->child;
  if (child == NULL)
    return grpc_json_create_child(NULL, parent, key, value, type, owns_value);
  while (child->next != NULL) {
    child = child->next;
  }
  return grpc_json_create_child(child, parent, key, value, type, owns_value);
}

static void recursively_populate_json(grpc_channel_tracer* tracer,
                                      tracer_tracker* tracker, grpc_json* json);

static void populate_node_data(grpc_trace_node* node, tracer_tracker* tracker,
                               grpc_json* json, grpc_json* children) {
  grpc_json* child = NULL;
  child = grpc_json_create_child(child, json, "data",
                                 grpc_slice_to_c_string(node->data),
                                 GRPC_JSON_STRING, true);
  if (node->error != GRPC_ERROR_NONE) {
    child = grpc_json_create_child(child, json, "error",
                                   gpr_strdup(grpc_error_string(node->error)),
                                   GRPC_JSON_STRING, true);
  }
  child =
      grpc_json_create_child(child, json, "time", fmt_time(node->time_created),
                             GRPC_JSON_STRING, true);
  child = grpc_json_create_child(
      child, json, "state",
      grpc_connectivity_state_name(node->connectivity_state), GRPC_JSON_STRING,
      false);
  if (node->referenced_tracer != NULL) {
    child = grpc_json_create_child(child, json, "id",
                                   gpr_strdup(node->referenced_tracer->id),
                                   GRPC_JSON_STRING, true);
    if (!check_tracer_tracked(tracker, node->referenced_tracer)) {
      grpc_json* referenced_tracer = create_child_find_brother(
          children, NULL, NULL, GRPC_JSON_OBJECT, false);
      recursively_populate_json(node->referenced_tracer, tracker,
                                referenced_tracer);
    }
  }
}

static void populate_node_list_data(grpc_channel_tracer* tracer,
                                    tracer_tracker* tracker, grpc_json* nodes,
                                    grpc_json* children) {
  grpc_json* child = NULL;
  grpc_trace_node* it = tracer->head_trace;
  while (it != NULL) {
    child = grpc_json_create_child(child, nodes, NULL, NULL, GRPC_JSON_OBJECT,
                                   false);
    populate_node_data(it, tracker, child, children);
    it = it->next;
  }
}

static void populate_tracer_data(grpc_channel_tracer* tracer,
                                 tracer_tracker* tracker,
                                 grpc_json* channel_data, grpc_json* children) {
  grpc_json* child = NULL;

  child =
      grpc_json_create_child(child, channel_data, "id", gpr_strdup(tracer->id),
                             GRPC_JSON_STRING, true);
  char* num_nodes_logged_str;
  gpr_asprintf(&num_nodes_logged_str, "%" PRId64, tracer->num_nodes_logged);
  child = grpc_json_create_child(child, channel_data, "numNodesLogged",
                                 num_nodes_logged_str, GRPC_JSON_NUMBER, true);
  child = grpc_json_create_child(child, channel_data, "startTime",
                                 fmt_time(tracer->time_created),
                                 GRPC_JSON_STRING, true);
  child = grpc_json_create_child(child, channel_data, "nodes", NULL,
                                 GRPC_JSON_ARRAY, false);
  populate_node_list_data(tracer, tracker, child, children);
}

static void recursively_populate_json(grpc_channel_tracer* tracer,
                                      tracer_tracker* tracker,
                                      grpc_json* json) {
  grpc_json* channel_data = grpc_json_create_child(
      NULL, json, "channelData", NULL, GRPC_JSON_OBJECT, false);
  grpc_json* children = grpc_json_create_child(channel_data, json, "children",
                                               NULL, GRPC_JSON_ARRAY, false);
  track_tracer(tracker, tracer);
  populate_tracer_data(tracer, tracker, channel_data, children);
}

char* grpc_channel_tracer_get_trace(grpc_channel_tracer* tracer) {
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);

  tracer_tracker tracker;
  memset(&tracker, 0, sizeof(tracker));

  recursively_populate_json(tracer, &tracker, json);

  gpr_free(tracker.tracers);

  char* json_str = grpc_json_dump_to_string(json, 0);
  grpc_json_destroy(json);
  return json_str;
}

#ifdef GRPC_CHANNEL_TRACER_REFCOUNT_DEBUG
void grpc_channel_tracer_log_trace(grpc_channel_tracer* tracer) {
  char* json_str = grpc_channel_tracer_get_trace(tracer);
  grpc_json* json = grpc_json_parse_string(json_str);
  char* fmt_json_str = grpc_json_dump_to_string(json, 1);
  gpr_log(GPR_DEBUG, "\n%s", fmt_json_str);
  gpr_free(fmt_json_str);
  grpc_json_destroy(json);
  gpr_free(json_str);
}
#endif
