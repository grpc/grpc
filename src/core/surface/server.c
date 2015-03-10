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

#include "src/core/surface/server.h"

#include <stdlib.h>
#include <string.h>

#include "src/core/channel/census_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/support/string.h"
#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"
#include "src/core/surface/init.h"
#include "src/core/transport/metadata.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

typedef enum { PENDING_START, ALL_CALLS, CALL_LIST_COUNT } call_list;

typedef struct listener {
  void *arg;
  void (*start)(grpc_server *server, void *arg, grpc_pollset **pollsets,
                size_t pollset_count);
  void (*destroy)(grpc_server *server, void *arg);
  struct listener *next;
} listener;

typedef struct call_data call_data;
typedef struct channel_data channel_data;
typedef struct registered_method registered_method;

typedef struct {
  call_data *next;
  call_data *prev;
} call_link;

typedef enum { LEGACY_CALL, BATCH_CALL, REGISTERED_CALL } requested_call_type;

typedef struct {
  requested_call_type type;
  void *tag;
  union {
    struct {
      grpc_completion_queue *cq_bind;
      grpc_call **call;
      grpc_call_details *details;
      grpc_metadata_array *initial_metadata;
    } batch;
    struct {
      grpc_completion_queue *cq_bind;
      grpc_call **call;
      registered_method *registered_method;
      gpr_timespec *deadline;
      grpc_metadata_array *initial_metadata;
      grpc_byte_buffer **optional_payload;
    } registered;
  } data;
} requested_call;

typedef struct {
  requested_call *calls;
  size_t count;
  size_t capacity;
} requested_call_array;

struct registered_method {
  char *method;
  char *host;
  call_data *pending;
  requested_call_array requested;
  grpc_completion_queue *cq;
  registered_method *next;
};

typedef struct channel_registered_method {
  registered_method *server_registered_method;
  grpc_mdstr *method;
  grpc_mdstr *host;
} channel_registered_method;

struct channel_data {
  grpc_server *server;
  grpc_channel *channel;
  grpc_mdstr *path_key;
  grpc_mdstr *authority_key;
  /* linked list of all channels on a server */
  channel_data *next;
  channel_data *prev;
  channel_registered_method *registered_methods;
  gpr_uint32 registered_method_slots;
  gpr_uint32 registered_method_max_probes;
};

struct grpc_server {
  size_t channel_filter_count;
  const grpc_channel_filter **channel_filters;
  grpc_channel_args *channel_args;
  grpc_completion_queue *unregistered_cq;

  grpc_completion_queue **cqs;
  grpc_pollset **pollsets;
  size_t cq_count;

  gpr_mu mu;

  registered_method *registered_methods;
  requested_call_array requested_calls;

  gpr_uint8 shutdown;
  size_t num_shutdown_tags;
  void **shutdown_tags;

  call_data *lists[CALL_LIST_COUNT];
  channel_data root_channel_data;

  listener *listeners;
  gpr_refcount internal_refcount;
};

typedef enum {
  /* waiting for metadata */
  NOT_STARTED,
  /* inital metadata read, not flow controlled in yet */
  PENDING,
  /* flow controlled in, on completion queue */
  ACTIVATED,
  /* cancelled before being queued */
  ZOMBIED
} call_state;

typedef struct legacy_data {
  grpc_metadata_array initial_metadata;
} legacy_data;

struct call_data {
  grpc_call *call;

  call_state state;
  gpr_timespec deadline;
  grpc_mdstr *path;
  grpc_mdstr *host;

  legacy_data *legacy;
  grpc_completion_queue *cq_new;

  call_data **root[CALL_LIST_COUNT];
  call_link links[CALL_LIST_COUNT];
};

#define SERVER_FROM_CALL_ELEM(elem) \
  (((channel_data *)(elem)->channel_data)->server)

static void do_nothing(void *unused, grpc_op_error ignored) {}

static void begin_call(grpc_server *server, call_data *calld,
                       requested_call *rc);
static void fail_call(grpc_server *server, requested_call *rc);

static int call_list_join(call_data **root, call_data *call, call_list list) {
  GPR_ASSERT(!call->root[list]);
  call->root[list] = root;
  if (!*root) {
    *root = call;
    call->links[list].next = call->links[list].prev = call;
  } else {
    call->links[list].next = *root;
    call->links[list].prev = (*root)->links[list].prev;
    call->links[list].next->links[list].prev =
        call->links[list].prev->links[list].next = call;
  }
  return 1;
}

static call_data *call_list_remove_head(call_data **root, call_list list) {
  call_data *out = *root;
  if (out) {
    out->root[list] = NULL;
    if (out->links[list].next == out) {
      *root = NULL;
    } else {
      *root = out->links[list].next;
      out->links[list].next->links[list].prev = out->links[list].prev;
      out->links[list].prev->links[list].next = out->links[list].next;
    }
  }
  return out;
}

static int call_list_remove(call_data *call, call_list list) {
  call_data **root = call->root[list];
  if (root == NULL) return 0;
  call->root[list] = NULL;
  if (*root == call) {
    *root = call->links[list].next;
    if (*root == call) {
      *root = NULL;
      return 1;
    }
  }
  GPR_ASSERT(*root != call);
  call->links[list].next->links[list].prev = call->links[list].prev;
  call->links[list].prev->links[list].next = call->links[list].next;
  return 1;
}

static void requested_call_array_destroy(requested_call_array *array) {
  gpr_free(array->calls);
}

static requested_call *requested_call_array_add(requested_call_array *array) {
  requested_call *rc;
  if (array->count == array->capacity) {
    array->capacity = GPR_MAX(array->capacity + 8, array->capacity * 2);
    array->calls =
        gpr_realloc(array->calls, sizeof(requested_call) * array->capacity);
  }
  rc = &array->calls[array->count++];
  memset(rc, 0, sizeof(*rc));
  return rc;
}

static void server_ref(grpc_server *server) {
  gpr_ref(&server->internal_refcount);
}

static void server_unref(grpc_server *server) {
  registered_method *rm;
  if (gpr_unref(&server->internal_refcount)) {
    grpc_channel_args_destroy(server->channel_args);
    gpr_mu_destroy(&server->mu);
    gpr_free(server->channel_filters);
    requested_call_array_destroy(&server->requested_calls);
    while ((rm = server->registered_methods) != NULL) {
      server->registered_methods = rm->next;
      gpr_free(rm->method);
      gpr_free(rm->host);
      requested_call_array_destroy(&rm->requested);
      gpr_free(rm);
    }
    gpr_free(server->cqs);
    gpr_free(server->pollsets);
    gpr_free(server->shutdown_tags);
    gpr_free(server);
  }
}

static int is_channel_orphaned(channel_data *chand) {
  return chand->next == chand;
}

static void orphan_channel(channel_data *chand) {
  chand->next->prev = chand->prev;
  chand->prev->next = chand->next;
  chand->next = chand->prev = chand;
}

static void finish_destroy_channel(void *cd, int success) {
  channel_data *chand = cd;
  grpc_server *server = chand->server;
  grpc_channel_internal_unref(chand->channel);
  server_unref(server);
}

static void destroy_channel(channel_data *chand) {
  if (is_channel_orphaned(chand)) return;
  GPR_ASSERT(chand->server != NULL);
  orphan_channel(chand);
  server_ref(chand->server);
  grpc_iomgr_add_callback(finish_destroy_channel, chand);
}

static void finish_start_new_rpc_and_unlock(grpc_server *server,
                                            grpc_call_element *elem,
                                            call_data **pending_root,
                                            requested_call_array *array) {
  requested_call rc;
  call_data *calld = elem->call_data;
  if (array->count == 0) {
    calld->state = PENDING;
    call_list_join(pending_root, calld, PENDING_START);
    gpr_mu_unlock(&server->mu);
  } else {
    rc = array->calls[--array->count];
    calld->state = ACTIVATED;
    gpr_mu_unlock(&server->mu);
    begin_call(server, calld, &rc);
  }
}

static void start_new_rpc(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  grpc_server *server = chand->server;
  gpr_uint32 i;
  gpr_uint32 hash;
  channel_registered_method *rm;

  gpr_mu_lock(&server->mu);
  if (chand->registered_methods && calld->path && calld->host) {
    /* TODO(ctiller): unify these two searches */
    /* check for an exact match with host */
    hash = GRPC_MDSTR_KV_HASH(calld->host->hash, calld->path->hash);
    for (i = 0; i < chand->registered_method_max_probes; i++) {
      rm = &chand->registered_methods[(hash + i) %
                                      chand->registered_method_slots];
      if (!rm) break;
      if (rm->host != calld->host) continue;
      if (rm->method != calld->path) continue;
      finish_start_new_rpc_and_unlock(server, elem,
                                      &rm->server_registered_method->pending,
                                      &rm->server_registered_method->requested);
      return;
    }
    /* check for a wildcard method definition (no host set) */
    hash = GRPC_MDSTR_KV_HASH(0, calld->path->hash);
    for (i = 0; i <= chand->registered_method_max_probes; i++) {
      rm = &chand->registered_methods[(hash + i) %
                                      chand->registered_method_slots];
      if (!rm) break;
      if (rm->host != NULL) continue;
      if (rm->method != calld->path) continue;
      finish_start_new_rpc_and_unlock(server, elem,
                                      &rm->server_registered_method->pending,
                                      &rm->server_registered_method->requested);
      return;
    }
  }
  finish_start_new_rpc_and_unlock(server, elem, &server->lists[PENDING_START],
                                  &server->requested_calls);
}

static void kill_zombie(void *elem, int success) {
  grpc_call_destroy(grpc_call_from_top_element(elem));
}

static void stream_closed(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->server->mu);
  switch (calld->state) {
    case ACTIVATED:
      break;
    case PENDING:
      call_list_remove(calld, PENDING_START);
    /* fallthrough intended */
    case NOT_STARTED:
      calld->state = ZOMBIED;
      grpc_iomgr_add_callback(kill_zombie, elem);
      break;
    case ZOMBIED:
      break;
  }
  gpr_mu_unlock(&chand->server->mu);
  grpc_call_stream_closed(elem);
}

static void read_closed(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->server->mu);
  switch (calld->state) {
    case ACTIVATED:
    case PENDING:
      grpc_call_read_closed(elem);
      break;
    case NOT_STARTED:
      calld->state = ZOMBIED;
      grpc_iomgr_add_callback(kill_zombie, elem);
      break;
    case ZOMBIED:
      break;
  }
  gpr_mu_unlock(&chand->server->mu);
}

static void call_op(grpc_call_element *elem, grpc_call_element *from_elemn,
                    grpc_call_op *op) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  grpc_mdelem *md;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  switch (op->type) {
    case GRPC_RECV_METADATA:
      md = op->data.metadata;
      if (md->key == chand->path_key) {
        calld->path = grpc_mdstr_ref(md->value);
        grpc_mdelem_unref(md);
      } else if (md->key == chand->authority_key) {
        calld->host = grpc_mdstr_ref(md->value);
        grpc_mdelem_unref(md);
      } else {
        grpc_call_recv_metadata(elem, md);
      }
      break;
    case GRPC_RECV_END_OF_INITIAL_METADATA:
      start_new_rpc(elem);
      grpc_call_initial_metadata_complete(elem);
      break;
    case GRPC_RECV_MESSAGE:
      grpc_call_recv_message(elem, op->data.message);
      op->done_cb(op->user_data, GRPC_OP_OK);
      break;
    case GRPC_RECV_HALF_CLOSE:
      read_closed(elem);
      break;
    case GRPC_RECV_FINISH:
      stream_closed(elem);
      break;
    case GRPC_RECV_DEADLINE:
      grpc_call_set_deadline(elem, op->data.deadline);
      ((call_data *)elem->call_data)->deadline = op->data.deadline;
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
      grpc_call_next_op(elem, op);
      break;
  }
}

static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  channel_data *chand = elem->channel_data;
  grpc_server *server = chand->server;

  switch (op->type) {
    case GRPC_ACCEPT_CALL:
      /* create a call */
      grpc_call_create(chand->channel, NULL,
                       op->data.accept_call.transport_server_data);
      break;
    case GRPC_TRANSPORT_CLOSED:
      /* if the transport is closed for a server channel, we destroy the
         channel */
      gpr_mu_lock(&server->mu);
      server_ref(server);
      destroy_channel(chand);
      gpr_mu_unlock(&server->mu);
      server_unref(server);
      break;
    case GRPC_TRANSPORT_GOAWAY:
      gpr_slice_unref(op->data.goaway.message);
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
      grpc_channel_next_op(elem, op);
      break;
  }
}

static void finish_shutdown_channel(void *cd, int success) {
  channel_data *chand = cd;
  grpc_channel_op op;
  op.type = GRPC_CHANNEL_DISCONNECT;
  op.dir = GRPC_CALL_DOWN;
  channel_op(grpc_channel_stack_element(
                 grpc_channel_get_channel_stack(chand->channel), 0),
             NULL, &op);
  grpc_channel_internal_unref(chand->channel);
}

static void shutdown_channel(channel_data *chand) {
  grpc_channel_internal_ref(chand->channel);
  grpc_iomgr_add_callback(finish_shutdown_channel, chand);
}

static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  memset(calld, 0, sizeof(call_data));
  calld->deadline = gpr_inf_future;
  calld->call = grpc_call_from_top_element(elem);

  gpr_mu_lock(&chand->server->mu);
  call_list_join(&chand->server->lists[ALL_CALLS], calld, ALL_CALLS);
  gpr_mu_unlock(&chand->server->mu);

  server_ref(chand->server);
}

static void destroy_call_elem(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  size_t i, j;

  gpr_mu_lock(&chand->server->mu);
  for (i = 0; i < CALL_LIST_COUNT; i++) {
    call_list_remove(elem->call_data, i);
  }
  if (chand->server->shutdown && chand->server->lists[ALL_CALLS] == NULL) {
    for (i = 0; i < chand->server->num_shutdown_tags; i++) {
      for (j = 0; j < chand->server->cq_count; j++) {
        grpc_cq_end_server_shutdown(chand->server->cqs[j],
                                    chand->server->shutdown_tags[i]);
      }
    }
  }
  gpr_mu_unlock(&chand->server->mu);

  if (calld->host) {
    grpc_mdstr_unref(calld->host);
  }
  if (calld->path) {
    grpc_mdstr_unref(calld->path);
  }

  if (calld->legacy) {
    gpr_free(calld->legacy->initial_metadata.metadata);
    gpr_free(calld->legacy);
  }

  server_unref(chand->server);
}

static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
  chand->server = NULL;
  chand->channel = NULL;
  chand->path_key = grpc_mdstr_from_string(metadata_context, ":path");
  chand->authority_key = grpc_mdstr_from_string(metadata_context, ":authority");
  chand->next = chand->prev = chand;
  chand->registered_methods = NULL;
}

static void destroy_channel_elem(grpc_channel_element *elem) {
  size_t i;
  channel_data *chand = elem->channel_data;
  if (chand->registered_methods) {
    for (i = 0; i < chand->registered_method_slots; i++) {
      if (chand->registered_methods[i].method) {
        grpc_mdstr_unref(chand->registered_methods[i].method);
      }
      if (chand->registered_methods[i].host) {
        grpc_mdstr_unref(chand->registered_methods[i].host);
      }
    }
    gpr_free(chand->registered_methods);
  }
  if (chand->server) {
    gpr_mu_lock(&chand->server->mu);
    chand->next->prev = chand->prev;
    chand->prev->next = chand->next;
    chand->next = chand->prev = chand;
    gpr_mu_unlock(&chand->server->mu);
    grpc_mdstr_unref(chand->path_key);
    grpc_mdstr_unref(chand->authority_key);
    server_unref(chand->server);
  }
}

static const grpc_channel_filter server_surface_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "server",
};

static void addcq(grpc_server *server, grpc_completion_queue *cq) {
  size_t i, n;
  for (i = 0; i < server->cq_count; i++) {
    if (server->cqs[i] == cq) return;
  }
  n = server->cq_count++;
  server->cqs = gpr_realloc(server->cqs,
                            server->cq_count * sizeof(grpc_completion_queue *));
  server->cqs[n] = cq;
}

grpc_server *grpc_server_create_from_filters(grpc_completion_queue *cq,
                                             grpc_channel_filter **filters,
                                             size_t filter_count,
                                             const grpc_channel_args *args) {
  size_t i;
  int census_enabled = grpc_channel_args_is_census_enabled(args);

  grpc_server *server = gpr_malloc(sizeof(grpc_server));

  GPR_ASSERT(grpc_is_initialized() && "call grpc_init()");

  memset(server, 0, sizeof(grpc_server));
  if (cq) addcq(server, cq);

  gpr_mu_init(&server->mu);

  server->unregistered_cq = cq;
  /* decremented by grpc_server_destroy */
  gpr_ref_init(&server->internal_refcount, 1);
  server->root_channel_data.next = server->root_channel_data.prev =
      &server->root_channel_data;

  /* Server filter stack is:

     server_surface_filter - for making surface API calls
     grpc_server_census_filter (optional) - for stats collection and tracing
     {passed in filter stack}
     grpc_connected_channel_filter - for interfacing with transports */
  server->channel_filter_count = filter_count + 1 + census_enabled;
  server->channel_filters =
      gpr_malloc(server->channel_filter_count * sizeof(grpc_channel_filter *));
  server->channel_filters[0] = &server_surface_filter;
  if (census_enabled) {
    server->channel_filters[1] = &grpc_server_census_filter;
  }
  for (i = 0; i < filter_count; i++) {
    server->channel_filters[i + 1 + census_enabled] = filters[i];
  }

  server->channel_args = grpc_channel_args_copy(args);

  return server;
}

static int streq(const char *a, const char *b) {
  if (a == NULL && b == NULL) return 1;
  if (a == NULL) return 0;
  if (b == NULL) return 0;
  return 0 == strcmp(a, b);
}

void *grpc_server_register_method(grpc_server *server, const char *method,
                                  const char *host,
                                  grpc_completion_queue *cq_new_rpc) {
  registered_method *m;
  if (!method) {
    gpr_log(GPR_ERROR, "%s method string cannot be NULL", __FUNCTION__);
    return NULL;
  }
  for (m = server->registered_methods; m; m = m->next) {
    if (streq(m->method, method) && streq(m->host, host)) {
      gpr_log(GPR_ERROR, "duplicate registration for %s@%s", method,
              host ? host : "*");
      return NULL;
    }
  }
  addcq(server, cq_new_rpc);
  m = gpr_malloc(sizeof(registered_method));
  memset(m, 0, sizeof(*m));
  m->method = gpr_strdup(method);
  m->host = gpr_strdup(host);
  m->next = server->registered_methods;
  m->cq = cq_new_rpc;
  server->registered_methods = m;
  return m;
}

void grpc_server_start(grpc_server *server) {
  listener *l;
  size_t i;

  server->pollsets = gpr_malloc(sizeof(grpc_pollset *) * server->cq_count);
  for (i = 0; i < server->cq_count; i++) {
    server->pollsets[i] = grpc_cq_pollset(server->cqs[i]);
  }

  for (l = server->listeners; l; l = l->next) {
    l->start(server, l->arg, server->pollsets, server->cq_count);
  }
}

grpc_transport_setup_result grpc_server_setup_transport(
    grpc_server *s, grpc_transport *transport,
    grpc_channel_filter const **extra_filters, size_t num_extra_filters,
    grpc_mdctx *mdctx) {
  size_t num_filters = s->channel_filter_count + num_extra_filters + 1;
  grpc_channel_filter const **filters =
      gpr_malloc(sizeof(grpc_channel_filter *) * num_filters);
  size_t i;
  size_t num_registered_methods;
  size_t alloc;
  registered_method *rm;
  channel_registered_method *crm;
  grpc_channel *channel;
  channel_data *chand;
  grpc_mdstr *host;
  grpc_mdstr *method;
  gpr_uint32 hash;
  gpr_uint32 slots;
  gpr_uint32 probes;
  gpr_uint32 max_probes = 0;
  grpc_transport_setup_result result;

  for (i = 0; i < s->channel_filter_count; i++) {
    filters[i] = s->channel_filters[i];
  }
  for (; i < s->channel_filter_count + num_extra_filters; i++) {
    filters[i] = extra_filters[i - s->channel_filter_count];
  }
  filters[i] = &grpc_connected_channel_filter;

  for (i = 0; i < s->cq_count; i++) {
    grpc_transport_add_to_pollset(transport, grpc_cq_pollset(s->cqs[i]));
  }

  channel = grpc_channel_create_from_filters(filters, num_filters,
                                             s->channel_args, mdctx, 0);
  chand = (channel_data *)grpc_channel_stack_element(
              grpc_channel_get_channel_stack(channel), 0)->channel_data;
  chand->server = s;
  server_ref(s);
  chand->channel = channel;

  num_registered_methods = 0;
  for (rm = s->registered_methods; rm; rm = rm->next) {
    num_registered_methods++;
  }
  /* build a lookup table phrased in terms of mdstr's in this channels context
     to quickly find registered methods */
  if (num_registered_methods > 0) {
    slots = 2 * num_registered_methods;
    alloc = sizeof(channel_registered_method) * slots;
    chand->registered_methods = gpr_malloc(alloc);
    memset(chand->registered_methods, 0, alloc);
    for (rm = s->registered_methods; rm; rm = rm->next) {
      host = rm->host ? grpc_mdstr_from_string(mdctx, rm->host) : NULL;
      method = grpc_mdstr_from_string(mdctx, rm->method);
      hash = GRPC_MDSTR_KV_HASH(host ? host->hash : 0, method->hash);
      for (probes = 0; chand->registered_methods[(hash + probes) % slots]
                               .server_registered_method != NULL;
           probes++)
        ;
      if (probes > max_probes) max_probes = probes;
      crm = &chand->registered_methods[(hash + probes) % slots];
      crm->server_registered_method = rm;
      crm->host = host;
      crm->method = method;
    }
    chand->registered_method_slots = slots;
    chand->registered_method_max_probes = max_probes;
  }

  result = grpc_connected_channel_bind_transport(
      grpc_channel_get_channel_stack(channel), transport);

  gpr_mu_lock(&s->mu);
  chand->next = &s->root_channel_data;
  chand->prev = chand->next->prev;
  chand->next->prev = chand->prev->next = chand;
  gpr_mu_unlock(&s->mu);

  gpr_free(filters);

  return result;
}

static void shutdown_internal(grpc_server *server, gpr_uint8 have_shutdown_tag,
                              void *shutdown_tag) {
  listener *l;
  requested_call_array requested_calls;
  channel_data **channels;
  channel_data *c;
  size_t nchannels;
  size_t i, j;
  grpc_channel_op op;
  grpc_channel_element *elem;
  registered_method *rm;

  /* lock, and gather up some stuff to do */
  gpr_mu_lock(&server->mu);
  if (have_shutdown_tag) {
    for (i = 0; i < server->cq_count; i++) {
      grpc_cq_begin_op(server->cqs[i], NULL, GRPC_SERVER_SHUTDOWN);
    }
    server->shutdown_tags =
        gpr_realloc(server->shutdown_tags,
                    sizeof(void *) * (server->num_shutdown_tags + 1));
    server->shutdown_tags[server->num_shutdown_tags++] = shutdown_tag;
  }
  if (server->shutdown) {
    gpr_mu_unlock(&server->mu);
    return;
  }

  nchannels = 0;
  for (c = server->root_channel_data.next; c != &server->root_channel_data;
       c = c->next) {
    nchannels++;
  }
  channels = gpr_malloc(sizeof(channel_data *) * nchannels);
  i = 0;
  for (c = server->root_channel_data.next; c != &server->root_channel_data;
       c = c->next) {
    grpc_channel_internal_ref(c->channel);
    channels[i] = c;
    i++;
  }

  /* collect all unregistered then registered calls */
  requested_calls = server->requested_calls;
  memset(&server->requested_calls, 0, sizeof(server->requested_calls));
  for (rm = server->registered_methods; rm; rm = rm->next) {
    if (requested_calls.count + rm->requested.count >
        requested_calls.capacity) {
      requested_calls.capacity =
          GPR_MAX(requested_calls.count + rm->requested.count,
                  2 * requested_calls.capacity);
      requested_calls.calls =
          gpr_realloc(requested_calls.calls, sizeof(*requested_calls.calls) *
                                                 requested_calls.capacity);
    }
    memcpy(requested_calls.calls + requested_calls.count, rm->requested.calls,
           sizeof(*requested_calls.calls) * rm->requested.count);
    requested_calls.count += rm->requested.count;
    gpr_free(rm->requested.calls);
    memset(&rm->requested, 0, sizeof(rm->requested));
  }

  server->shutdown = 1;
  if (server->lists[ALL_CALLS] == NULL) {
    for (i = 0; i < server->num_shutdown_tags; i++) {
      for (j = 0; j < server->cq_count; j++) {
        grpc_cq_end_server_shutdown(server->cqs[j], server->shutdown_tags[i]);
      }
    }
  }
  gpr_mu_unlock(&server->mu);

  for (i = 0; i < nchannels; i++) {
    c = channels[i];
    elem = grpc_channel_stack_element(
        grpc_channel_get_channel_stack(c->channel), 0);

    op.type = GRPC_CHANNEL_GOAWAY;
    op.dir = GRPC_CALL_DOWN;
    op.data.goaway.status = GRPC_STATUS_OK;
    op.data.goaway.message = gpr_slice_from_copied_string("Server shutdown");
    elem->filter->channel_op(elem, NULL, &op);

    grpc_channel_internal_unref(c->channel);
  }
  gpr_free(channels);

  /* terminate all the requested calls */
  for (i = 0; i < requested_calls.count; i++) {
    fail_call(server, &requested_calls.calls[i]);
  }
  gpr_free(requested_calls.calls);

  /* Shutdown listeners */
  for (l = server->listeners; l; l = l->next) {
    l->destroy(server, l->arg);
  }
  while (server->listeners) {
    l = server->listeners;
    server->listeners = l->next;
    gpr_free(l);
  }
}

void grpc_server_shutdown(grpc_server *server) {
  shutdown_internal(server, 0, NULL);
}

void grpc_server_shutdown_and_notify(grpc_server *server, void *tag) {
  shutdown_internal(server, 1, tag);
}

void grpc_server_destroy(grpc_server *server) {
  channel_data *c;
  gpr_mu_lock(&server->mu);
  if (!server->shutdown) {
    gpr_mu_unlock(&server->mu);
    grpc_server_shutdown(server);
    gpr_mu_lock(&server->mu);
  }

  for (c = server->root_channel_data.next; c != &server->root_channel_data;
       c = c->next) {
    shutdown_channel(c);
  }
  gpr_mu_unlock(&server->mu);

  server_unref(server);
}

void grpc_server_add_listener(grpc_server *server, void *arg,
                              void (*start)(grpc_server *server, void *arg,
                                            grpc_pollset **pollsets,
                                            size_t pollset_count),
                              void (*destroy)(grpc_server *server, void *arg)) {
  listener *l = gpr_malloc(sizeof(listener));
  l->arg = arg;
  l->start = start;
  l->destroy = destroy;
  l->next = server->listeners;
  server->listeners = l;
}

static grpc_call_error queue_call_request(grpc_server *server,
                                          requested_call *rc) {
  call_data *calld = NULL;
  requested_call_array *requested_calls = NULL;
  gpr_mu_lock(&server->mu);
  if (server->shutdown) {
    gpr_mu_unlock(&server->mu);
    fail_call(server, rc);
    return GRPC_CALL_OK;
  }
  switch (rc->type) {
    case LEGACY_CALL:
    case BATCH_CALL:
      calld =
          call_list_remove_head(&server->lists[PENDING_START], PENDING_START);
      requested_calls = &server->requested_calls;
      break;
    case REGISTERED_CALL:
      calld = call_list_remove_head(
          &rc->data.registered.registered_method->pending, PENDING_START);
      requested_calls = &rc->data.registered.registered_method->requested;
      break;
  }
  if (calld) {
    GPR_ASSERT(calld->state == PENDING);
    calld->state = ACTIVATED;
    gpr_mu_unlock(&server->mu);
    begin_call(server, calld, rc);
    return GRPC_CALL_OK;
  } else {
    *requested_call_array_add(requested_calls) = *rc;
    gpr_mu_unlock(&server->mu);
    return GRPC_CALL_OK;
  }
}

grpc_call_error grpc_server_request_call(grpc_server *server, grpc_call **call,
                                         grpc_call_details *details,
                                         grpc_metadata_array *initial_metadata,
                                         grpc_completion_queue *cq_bind,
                                         void *tag) {
  requested_call rc;
  grpc_cq_begin_op(server->unregistered_cq, NULL, GRPC_OP_COMPLETE);
  rc.type = BATCH_CALL;
  rc.tag = tag;
  rc.data.batch.cq_bind = cq_bind;
  rc.data.batch.call = call;
  rc.data.batch.details = details;
  rc.data.batch.initial_metadata = initial_metadata;
  return queue_call_request(server, &rc);
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *rm, grpc_call **call, gpr_timespec *deadline,
    grpc_metadata_array *initial_metadata, grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bind, void *tag) {
  requested_call rc;
  registered_method *registered_method = rm;
  grpc_cq_begin_op(registered_method->cq, NULL, GRPC_OP_COMPLETE);
  rc.type = REGISTERED_CALL;
  rc.tag = tag;
  rc.data.registered.cq_bind = cq_bind;
  rc.data.registered.call = call;
  rc.data.registered.registered_method = registered_method;
  rc.data.registered.deadline = deadline;
  rc.data.registered.initial_metadata = initial_metadata;
  rc.data.registered.optional_payload = optional_payload;
  return queue_call_request(server, &rc);
}

grpc_call_error grpc_server_request_call_old(grpc_server *server,
                                             void *tag_new) {
  requested_call rc;
  grpc_cq_begin_op(server->unregistered_cq, NULL, GRPC_SERVER_RPC_NEW);
  rc.type = LEGACY_CALL;
  rc.tag = tag_new;
  return queue_call_request(server, &rc);
}

static void publish_legacy(grpc_call *call, grpc_op_error status, void *tag);
static void publish_registered_or_batch(grpc_call *call, grpc_op_error status,
                                        void *tag);
static void publish_was_not_set(grpc_call *call, grpc_op_error status,
                                void *tag) {
  abort();
}

static void cpstr(char **dest, size_t *capacity, grpc_mdstr *value) {
  gpr_slice slice = value->slice;
  size_t len = GPR_SLICE_LENGTH(slice);

  if (len + 1 > *capacity) {
    *capacity = GPR_MAX(len + 1, *capacity * 2);
    *dest = gpr_realloc(*dest, *capacity);
  }
  memcpy(*dest, grpc_mdstr_as_c_string(value), len + 1);
}

static void begin_call(grpc_server *server, call_data *calld,
                       requested_call *rc) {
  grpc_ioreq_completion_func publish = publish_was_not_set;
  grpc_ioreq req[2];
  grpc_ioreq *r = req;

  /* called once initial metadata has been read by the call, but BEFORE
     the ioreq to fetch it out of the call has been executed.
     This means metadata related fields can be relied on in calld, but to
     fill in the metadata array passed by the client, we need to perform
     an ioreq op, that should complete immediately. */

  switch (rc->type) {
    case LEGACY_CALL:
      calld->legacy = gpr_malloc(sizeof(legacy_data));
      memset(calld->legacy, 0, sizeof(legacy_data));
      r->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
      r->data.recv_metadata = &calld->legacy->initial_metadata;
      r++;
      publish = publish_legacy;
      break;
    case BATCH_CALL:
      cpstr(&rc->data.batch.details->host,
            &rc->data.batch.details->host_capacity, calld->host);
      cpstr(&rc->data.batch.details->method,
            &rc->data.batch.details->method_capacity, calld->path);
      grpc_call_set_completion_queue(calld->call, rc->data.batch.cq_bind);
      *rc->data.batch.call = calld->call;
      r->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
      r->data.recv_metadata = rc->data.batch.initial_metadata;
      r++;
      calld->cq_new = server->unregistered_cq;
      publish = publish_registered_or_batch;
      break;
    case REGISTERED_CALL:
      *rc->data.registered.deadline = calld->deadline;
      grpc_call_set_completion_queue(calld->call, rc->data.registered.cq_bind);
      *rc->data.registered.call = calld->call;
      r->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
      r->data.recv_metadata = rc->data.registered.initial_metadata;
      r++;
      if (rc->data.registered.optional_payload) {
        r->op = GRPC_IOREQ_RECV_MESSAGE;
        r->data.recv_message = rc->data.registered.optional_payload;
        r++;
      }
      calld->cq_new = rc->data.registered.registered_method->cq;
      publish = publish_registered_or_batch;
      break;
  }

  grpc_call_internal_ref(calld->call);
  grpc_call_start_ioreq_and_call_back(calld->call, req, r - req, publish,
                                      rc->tag);
}

static void fail_call(grpc_server *server, requested_call *rc) {
  switch (rc->type) {
    case LEGACY_CALL:
      grpc_cq_end_new_rpc(server->unregistered_cq, rc->tag, NULL, do_nothing,
                          NULL, NULL, NULL, gpr_inf_past, 0, NULL);
      break;
    case BATCH_CALL:
      *rc->data.batch.call = NULL;
      rc->data.batch.initial_metadata->count = 0;
      grpc_cq_end_op_complete(server->unregistered_cq, rc->tag, NULL,
                              do_nothing, NULL, GRPC_OP_ERROR);
      break;
    case REGISTERED_CALL:
      *rc->data.registered.call = NULL;
      rc->data.registered.initial_metadata->count = 0;
      grpc_cq_end_op_complete(rc->data.registered.registered_method->cq,
                              rc->tag, NULL, do_nothing, NULL, GRPC_OP_ERROR);
      break;
  }
}

static void publish_legacy(grpc_call *call, grpc_op_error status, void *tag) {
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_server *server = chand->server;

  if (status == GRPC_OP_OK) {
    grpc_cq_end_new_rpc(server->unregistered_cq, tag, call, do_nothing, NULL,
                        grpc_mdstr_as_c_string(calld->path),
                        grpc_mdstr_as_c_string(calld->host), calld->deadline,
                        calld->legacy->initial_metadata.count,
                        calld->legacy->initial_metadata.metadata);
  } else {
    gpr_log(GPR_ERROR, "should never reach here");
    abort();
  }
}

static void publish_registered_or_batch(grpc_call *call, grpc_op_error status,
                                        void *tag) {
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  grpc_cq_end_op_complete(calld->cq_new, tag, call, do_nothing, NULL, status);
}

const grpc_channel_args *grpc_server_get_channel_args(grpc_server *server) {
  return server->channel_args;
}
