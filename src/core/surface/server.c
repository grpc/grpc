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
#include <grpc/support/string_util.h>
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

typedef enum { BATCH_CALL, REGISTERED_CALL } requested_call_type;

typedef struct {
  requested_call_type type;
  void *tag;
  grpc_completion_queue *cq_bound_to_call;
  grpc_completion_queue *cq_for_notification;
  grpc_call **call;
  union {
    struct {
      grpc_call_details *details;
      grpc_metadata_array *initial_metadata;
    } batch;
    struct {
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
  registered_method *next;
};

typedef struct channel_registered_method {
  registered_method *server_registered_method;
  grpc_mdstr *method;
  grpc_mdstr *host;
} channel_registered_method;

struct channel_data {
  grpc_server *server;
  size_t num_calls;
  grpc_channel *channel;
  grpc_mdstr *path_key;
  grpc_mdstr *authority_key;
  /* linked list of all channels on a server */
  channel_data *next;
  channel_data *prev;
  channel_registered_method *registered_methods;
  gpr_uint32 registered_method_slots;
  gpr_uint32 registered_method_max_probes;
  grpc_iomgr_closure finish_destroy_channel_closure;
};

typedef struct shutdown_tag {
  void *tag;
  grpc_completion_queue *cq;
} shutdown_tag;

struct grpc_server {
  size_t channel_filter_count;
  const grpc_channel_filter **channel_filters;
  grpc_channel_args *channel_args;

  grpc_completion_queue **cqs;
  grpc_pollset **pollsets;
  size_t cq_count;

  /* The two following mutexes control access to server-state
     mu_global controls access to non-call-related state (e.g., channel state)
     mu_call controls access to call-related state (e.g., the call lists)

     If they are ever required to be nested, you must lock mu_global
     before mu_call. This is currently used in shutdown processing
     (grpc_server_shutdown_and_notify and maybe_finish_shutdown) */
  gpr_mu mu_global; /* mutex for server and channel state */
  gpr_mu mu_call; /* mutex for call-specific state */

  registered_method *registered_methods;
  requested_call_array requested_calls;

  gpr_uint8 shutdown;
  gpr_uint8 shutdown_published;
  size_t num_shutdown_tags;
  shutdown_tag *shutdown_tags;

  call_data *lists[CALL_LIST_COUNT];
  channel_data root_channel_data;

  listener *listeners;
  int listeners_destroyed;
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

struct call_data {
  grpc_call *call;

  call_state state;
  grpc_mdstr *path;
  grpc_mdstr *host;
  gpr_timespec deadline;
  int got_initial_metadata;

  grpc_completion_queue *cq_new;

  grpc_stream_op_buffer *recv_ops;
  grpc_stream_state *recv_state;
  void (*on_done_recv)(void *user_data, int success);
  void *recv_user_data;

  grpc_iomgr_closure kill_zombie_closure;

  call_data **root[CALL_LIST_COUNT];
  call_link links[CALL_LIST_COUNT];
};

#define SERVER_FROM_CALL_ELEM(elem) \
  (((channel_data *)(elem)->channel_data)->server)

static void begin_call(grpc_server *server, call_data *calld,
                       requested_call *rc);
static void fail_call(grpc_server *server, requested_call *rc);
static void shutdown_channel(channel_data *chand, int send_goaway,
                             int send_disconnect);
/* Before calling maybe_finish_shutdown, we must hold mu_global and not
   hold mu_call */
static void maybe_finish_shutdown(grpc_server *server);

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

static void server_delete(grpc_server *server) {
  registered_method *rm;
  size_t i;
  grpc_channel_args_destroy(server->channel_args);
  gpr_mu_destroy(&server->mu_global);
  gpr_mu_destroy(&server->mu_call);
  gpr_free(server->channel_filters);
  requested_call_array_destroy(&server->requested_calls);
  while ((rm = server->registered_methods) != NULL) {
    server->registered_methods = rm->next;
    gpr_free(rm->method);
    gpr_free(rm->host);
    requested_call_array_destroy(&rm->requested);
    gpr_free(rm);
  }
  for (i = 0; i < server->cq_count; i++) {
    GRPC_CQ_INTERNAL_UNREF(server->cqs[i], "server");
  }
  gpr_free(server->cqs);
  gpr_free(server->pollsets);
  gpr_free(server->shutdown_tags);
  gpr_free(server);
}

static void server_unref(grpc_server *server) {
  if (gpr_unref(&server->internal_refcount)) {
    server_delete(server);
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
  GRPC_CHANNEL_INTERNAL_UNREF(chand->channel, "server");
  server_unref(server);
}

static void destroy_channel(channel_data *chand) {
  if (is_channel_orphaned(chand)) return;
  GPR_ASSERT(chand->server != NULL);
  orphan_channel(chand);
  server_ref(chand->server);
  maybe_finish_shutdown(chand->server);
  chand->finish_destroy_channel_closure.cb = finish_destroy_channel;
  chand->finish_destroy_channel_closure.cb_arg = chand;
  grpc_iomgr_add_callback(&chand->finish_destroy_channel_closure);
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
    gpr_mu_unlock(&server->mu_call);
  } else {
    rc = array->calls[--array->count];
    calld->state = ACTIVATED;
    gpr_mu_unlock(&server->mu_call);
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

  gpr_mu_lock(&server->mu_call);
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

static int num_listeners(grpc_server *server) {
  listener *l;
  int n = 0;
  for (l = server->listeners; l; l = l->next) {
    n++;
  }
  return n;
}

static void maybe_finish_shutdown(grpc_server *server) {
  size_t i;
  if (!server->shutdown || server->shutdown_published) {
    return;
  }

  gpr_mu_lock(&server->mu_call);
  if (server->lists[ALL_CALLS] != NULL) {
    gpr_log(GPR_DEBUG,
            "Waiting for all calls to finish before destroying server");
    gpr_mu_unlock(&server->mu_call);
    return;
  }
  gpr_mu_unlock(&server->mu_call);

  if (server->root_channel_data.next != &server->root_channel_data) {
    gpr_log(GPR_DEBUG,
            "Waiting for all channels to close before destroying server");
    return;
  }
  if (server->listeners_destroyed < num_listeners(server)) {
    gpr_log(GPR_DEBUG, "Waiting for all listeners to be destroyed (@ %d/%d)",
            server->listeners_destroyed, num_listeners(server));
    return;
  }
  server->shutdown_published = 1;
  for (i = 0; i < server->num_shutdown_tags; i++) {
    grpc_cq_end_op(server->shutdown_tags[i].cq, server->shutdown_tags[i].tag,
                   NULL, 1);
  }
}

static grpc_mdelem *server_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (md->key == chand->path_key) {
    calld->path = grpc_mdstr_ref(md->value);
    return NULL;
  } else if (md->key == chand->authority_key) {
    calld->host = grpc_mdstr_ref(md->value);
    return NULL;
  }
  return md;
}

static void decrement_call_count(channel_data *chand) {
  chand->num_calls--;
  if (0 == chand->num_calls && chand->server->shutdown) {
    shutdown_channel(chand, 0, 1);
  }
  maybe_finish_shutdown(chand->server);
}

static void server_on_recv(void *ptr, int success) {
  grpc_call_element *elem = ptr;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  int remove_res;

  if (success && !calld->got_initial_metadata) {
    size_t i;
    size_t nops = calld->recv_ops->nops;
    grpc_stream_op *ops = calld->recv_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *op = &ops[i];
      if (op->type != GRPC_OP_METADATA) continue;
      grpc_metadata_batch_filter(&op->data.metadata, server_filter, elem);
      if (0 != gpr_time_cmp(op->data.metadata.deadline, gpr_inf_future)) {
        calld->deadline = op->data.metadata.deadline;
      }
      calld->got_initial_metadata = 1;
      start_new_rpc(elem);
      break;
    }
  }

  switch (*calld->recv_state) {
    case GRPC_STREAM_OPEN:
      break;
    case GRPC_STREAM_SEND_CLOSED:
      break;
    case GRPC_STREAM_RECV_CLOSED:
      gpr_mu_lock(&chand->server->mu_call);
      if (calld->state == NOT_STARTED) {
        calld->state = ZOMBIED;
        grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      }
      gpr_mu_unlock(&chand->server->mu_call);
      break;
    case GRPC_STREAM_CLOSED:
      gpr_mu_lock(&chand->server->mu_call);
      if (calld->state == NOT_STARTED) {
        calld->state = ZOMBIED;
        grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      } else if (calld->state == PENDING) {
        call_list_remove(calld, PENDING_START);
        calld->state = ZOMBIED;
        grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      }
      remove_res = call_list_remove(calld, ALL_CALLS);
      gpr_mu_unlock(&chand->server->mu_call);
      gpr_mu_lock(&chand->server->mu_global);
      if (remove_res) {
        decrement_call_count(chand);
      }
      gpr_mu_unlock(&chand->server->mu_global);
      break;
  }

  calld->on_done_recv(calld->recv_user_data, success);
}

static void server_mutate_op(grpc_call_element *elem,
                             grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;

  if (op->recv_ops) {
    /* substitute our callback for the higher callback */
    calld->recv_ops = op->recv_ops;
    calld->recv_state = op->recv_state;
    calld->on_done_recv = op->on_done_recv;
    calld->recv_user_data = op->recv_user_data;
    op->on_done_recv = server_on_recv;
    op->recv_user_data = elem;
  }
}

static void server_start_transport_op(grpc_call_element *elem,
                                      grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  server_mutate_op(elem, op);
  grpc_call_next_op(elem, op);
}

static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  channel_data *chand = elem->channel_data;
  grpc_server *server = chand->server;

  switch (op->type) {
    case GRPC_ACCEPT_CALL:
      /* create a call */
      grpc_call_create(chand->channel, NULL,
                       op->data.accept_call.transport_server_data, NULL, 0,
                       gpr_inf_future);
      break;
    case GRPC_TRANSPORT_CLOSED:
      /* if the transport is closed for a server channel, we destroy the
         channel */
      gpr_mu_lock(&server->mu_global);
      server_ref(server);
      destroy_channel(chand);
      gpr_mu_unlock(&server->mu_global);
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

typedef struct {
  channel_data *chand;
  int send_goaway;
  int send_disconnect;
  grpc_iomgr_closure finish_shutdown_channel_closure;
} shutdown_channel_args;

static void finish_shutdown_channel(void *p, int success) {
  shutdown_channel_args *sca = p;
  grpc_channel_op op;

  if (sca->send_goaway) {
    op.type = GRPC_CHANNEL_GOAWAY;
    op.dir = GRPC_CALL_DOWN;
    op.data.goaway.status = GRPC_STATUS_OK;
    op.data.goaway.message = gpr_slice_from_copied_string("Server shutdown");
    channel_op(grpc_channel_stack_element(
                   grpc_channel_get_channel_stack(sca->chand->channel), 0),
               NULL, &op);
  }
  if (sca->send_disconnect) {
    op.type = GRPC_CHANNEL_DISCONNECT;
    op.dir = GRPC_CALL_DOWN;
    channel_op(grpc_channel_stack_element(
                   grpc_channel_get_channel_stack(sca->chand->channel), 0),
               NULL, &op);
  }
  GRPC_CHANNEL_INTERNAL_UNREF(sca->chand->channel, "shutdown");

  gpr_free(sca);
}

static void shutdown_channel(channel_data *chand, int send_goaway,
                             int send_disconnect) {
  shutdown_channel_args *sca;
  GRPC_CHANNEL_INTERNAL_REF(chand->channel, "shutdown");
  sca = gpr_malloc(sizeof(shutdown_channel_args));
  sca->chand = chand;
  sca->send_goaway = send_goaway;
  sca->send_disconnect = send_disconnect;
  sca->finish_shutdown_channel_closure.cb = finish_shutdown_channel;
  sca->finish_shutdown_channel_closure.cb_arg = sca;
  grpc_iomgr_add_callback(&sca->finish_shutdown_channel_closure);
}

static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  memset(calld, 0, sizeof(call_data));
  calld->deadline = gpr_inf_future;
  calld->call = grpc_call_from_top_element(elem);

  gpr_mu_lock(&chand->server->mu_call);
  call_list_join(&chand->server->lists[ALL_CALLS], calld, ALL_CALLS);
  gpr_mu_unlock(&chand->server->mu_call);

  gpr_mu_lock(&chand->server->mu_global);
  chand->num_calls++;
  gpr_mu_unlock(&chand->server->mu_global);

  server_ref(chand->server);

  if (initial_op) server_mutate_op(elem, initial_op);
}

static void destroy_call_elem(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  int removed[CALL_LIST_COUNT];
  size_t i;

  gpr_mu_lock(&chand->server->mu_call);
  for (i = 0; i < CALL_LIST_COUNT; i++) {
    removed[i] = call_list_remove(elem->call_data, i);
  }
  gpr_mu_unlock(&chand->server->mu_call);
  if (removed[ALL_CALLS]) {
    gpr_mu_lock(&chand->server->mu_global);
    decrement_call_count(chand);
    gpr_mu_unlock(&chand->server->mu_global);
  }

  if (calld->host) {
    grpc_mdstr_unref(calld->host);
  }
  if (calld->path) {
    grpc_mdstr_unref(calld->path);
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
  chand->num_calls = 0;
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
    gpr_mu_lock(&chand->server->mu_global);
    chand->next->prev = chand->prev;
    chand->prev->next = chand->next;
    chand->next = chand->prev = chand;
    maybe_finish_shutdown(chand->server);
    gpr_mu_unlock(&chand->server->mu_global);
    grpc_mdstr_unref(chand->path_key);
    grpc_mdstr_unref(chand->authority_key);
    server_unref(chand->server);
  }
}

static const grpc_channel_filter server_surface_filter = {
    server_start_transport_op,
    channel_op,
    sizeof(call_data),
    init_call_elem,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    "server",
};

void grpc_server_register_completion_queue(grpc_server *server,
                                           grpc_completion_queue *cq) {
  size_t i, n;
  for (i = 0; i < server->cq_count; i++) {
    if (server->cqs[i] == cq) return;
  }
  GRPC_CQ_INTERNAL_REF(cq, "server");
  grpc_cq_mark_server_cq(cq);
  n = server->cq_count++;
  server->cqs = gpr_realloc(server->cqs,
                            server->cq_count * sizeof(grpc_completion_queue *));
  server->cqs[n] = cq;
}

grpc_server *grpc_server_create_from_filters(grpc_channel_filter **filters,
                                             size_t filter_count,
                                             const grpc_channel_args *args) {
  size_t i;
  /* TODO(census): restore this once we finalize census filter etc.
     int census_enabled = grpc_channel_args_is_census_enabled(args); */
  int census_enabled = 0;

  grpc_server *server = gpr_malloc(sizeof(grpc_server));

  GPR_ASSERT(grpc_is_initialized() && "call grpc_init()");

  memset(server, 0, sizeof(grpc_server));

  gpr_mu_init(&server->mu_global);
  gpr_mu_init(&server->mu_call);

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
  /* TODO(census): restore this once we rework census filter
  if (census_enabled) {
    server->channel_filters[1] = &grpc_server_census_filter;
    } */
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
                                  const char *host) {
  registered_method *m;
  if (!method) {
    gpr_log(GPR_ERROR,
            "grpc_server_register_method method string cannot be NULL");
    return NULL;
  }
  for (m = server->registered_methods; m; m = m->next) {
    if (streq(m->method, method) && streq(m->host, host)) {
      gpr_log(GPR_ERROR, "duplicate registration for %s@%s", method,
              host ? host : "*");
      return NULL;
    }
  }
  m = gpr_malloc(sizeof(registered_method));
  memset(m, 0, sizeof(*m));
  m->method = gpr_strdup(method);
  m->host = gpr_strdup(host);
  m->next = server->registered_methods;
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
    grpc_mdctx *mdctx, const grpc_channel_args *args) {
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

  channel =
      grpc_channel_create_from_filters(filters, num_filters, args, mdctx, 0);
  chand = (channel_data *)grpc_channel_stack_element(
              grpc_channel_get_channel_stack(channel), 0)
              ->channel_data;
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

  gpr_mu_lock(&s->mu_global);
  chand->next = &s->root_channel_data;
  chand->prev = chand->next->prev;
  chand->next->prev = chand->prev->next = chand;
  gpr_mu_unlock(&s->mu_global);

  gpr_free(filters);

  return result;
}

void grpc_server_shutdown_and_notify(grpc_server *server,
                                     grpc_completion_queue *cq, void *tag) {
  listener *l;
  requested_call_array requested_calls;
  channel_data *c;
  size_t i;
  registered_method *rm;
  shutdown_tag *sdt;

  /* lock, and gather up some stuff to do */
  gpr_mu_lock(&server->mu_global);
  grpc_cq_begin_op(cq, NULL);
  server->shutdown_tags =
      gpr_realloc(server->shutdown_tags,
                  sizeof(shutdown_tag) * (server->num_shutdown_tags + 1));
  sdt = &server->shutdown_tags[server->num_shutdown_tags++];
  sdt->tag = tag;
  sdt->cq = cq;
  if (server->shutdown) {
    gpr_mu_unlock(&server->mu_global);
    return;
  }

  for (c = server->root_channel_data.next; c != &server->root_channel_data;
       c = c->next) {
    shutdown_channel(c, 1, c->num_calls == 0);
  }

  /* collect all unregistered then registered calls */
  gpr_mu_lock(&server->mu_call);
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
  gpr_mu_unlock(&server->mu_call);

  server->shutdown = 1;
  maybe_finish_shutdown(server);
  gpr_mu_unlock(&server->mu_global);

  /* terminate all the requested calls */
  for (i = 0; i < requested_calls.count; i++) {
    fail_call(server, &requested_calls.calls[i]);
  }
  gpr_free(requested_calls.calls);

  /* Shutdown listeners */
  for (l = server->listeners; l; l = l->next) {
    l->destroy(server, l->arg);
  }
}

void grpc_server_listener_destroy_done(void *s) {
  grpc_server *server = s;
  gpr_mu_lock(&server->mu_global);
  server->listeners_destroyed++;
  maybe_finish_shutdown(server);
  gpr_mu_unlock(&server->mu_global);
}

void grpc_server_cancel_all_calls(grpc_server *server) {
  call_data *calld;
  grpc_call **calls;
  size_t call_count;
  size_t call_capacity;
  int is_first = 1;
  size_t i;

  gpr_mu_lock(&server->mu_call);

  GPR_ASSERT(server->shutdown);

  if (!server->lists[ALL_CALLS]) {
    gpr_mu_unlock(&server->mu_call);
    return;
  }

  call_capacity = 8;
  call_count = 0;
  calls = gpr_malloc(sizeof(grpc_call *) * call_capacity);

  for (calld = server->lists[ALL_CALLS];
       calld != server->lists[ALL_CALLS] || is_first;
       calld = calld->links[ALL_CALLS].next) {
    if (call_count == call_capacity) {
      call_capacity *= 2;
      calls = gpr_realloc(calls, sizeof(grpc_call *) * call_capacity);
    }
    calls[call_count++] = calld->call;
    GRPC_CALL_INTERNAL_REF(calld->call, "cancel_all");
    is_first = 0;
  }

  gpr_mu_unlock(&server->mu_call);

  for (i = 0; i < call_count; i++) {
    grpc_call_cancel_with_status(calls[i], GRPC_STATUS_UNAVAILABLE,
                                 "Unavailable");
    GRPC_CALL_INTERNAL_UNREF(calls[i], "cancel_all", 1);
  }

  gpr_free(calls);
}

void grpc_server_destroy(grpc_server *server) {
  listener *l;

  gpr_mu_lock(&server->mu_global);
  GPR_ASSERT(server->shutdown || !server->listeners);
  GPR_ASSERT(server->listeners_destroyed == num_listeners(server));

  while (server->listeners) {
    l = server->listeners;
    server->listeners = l->next;
    gpr_free(l);
  }

  gpr_mu_unlock(&server->mu_global);

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
  gpr_mu_lock(&server->mu_call);
  if (server->shutdown) {
    gpr_mu_unlock(&server->mu_call);
    fail_call(server, rc);
    return GRPC_CALL_OK;
  }
  switch (rc->type) {
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
    gpr_mu_unlock(&server->mu_call);
    begin_call(server, calld, rc);
    return GRPC_CALL_OK;
  } else {
    *requested_call_array_add(requested_calls) = *rc;
    gpr_mu_unlock(&server->mu_call);
    return GRPC_CALL_OK;
  }
}

grpc_call_error grpc_server_request_call(
    grpc_server *server, grpc_call **call, grpc_call_details *details,
    grpc_metadata_array *initial_metadata,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag) {
  requested_call rc;
  GRPC_SERVER_LOG_REQUEST_CALL(GPR_INFO, server, call, details,
                               initial_metadata, cq_bound_to_call,
                               cq_for_notification, tag);
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    return GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
  }
  grpc_cq_begin_op(cq_for_notification, NULL);
  rc.type = BATCH_CALL;
  rc.tag = tag;
  rc.cq_bound_to_call = cq_bound_to_call;
  rc.cq_for_notification = cq_for_notification;
  rc.call = call;
  rc.data.batch.details = details;
  rc.data.batch.initial_metadata = initial_metadata;
  return queue_call_request(server, &rc);
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *rm, grpc_call **call, gpr_timespec *deadline,
    grpc_metadata_array *initial_metadata, grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag) {
  requested_call rc;
  registered_method *registered_method = rm;
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    return GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
  }
  grpc_cq_begin_op(cq_for_notification, NULL);
  rc.type = REGISTERED_CALL;
  rc.tag = tag;
  rc.cq_bound_to_call = cq_bound_to_call;
  rc.cq_for_notification = cq_for_notification;
  rc.call = call;
  rc.data.registered.registered_method = registered_method;
  rc.data.registered.deadline = deadline;
  rc.data.registered.initial_metadata = initial_metadata;
  rc.data.registered.optional_payload = optional_payload;
  return queue_call_request(server, &rc);
}

static void publish_registered_or_batch(grpc_call *call, int success,
                                        void *tag);
static void publish_was_not_set(grpc_call *call, int success, void *tag) {
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

  grpc_call_set_completion_queue(calld->call, rc->cq_bound_to_call);
  *rc->call = calld->call;
  calld->cq_new = rc->cq_for_notification;
  switch (rc->type) {
    case BATCH_CALL:
      cpstr(&rc->data.batch.details->host,
            &rc->data.batch.details->host_capacity, calld->host);
      cpstr(&rc->data.batch.details->method,
            &rc->data.batch.details->method_capacity, calld->path);
      rc->data.batch.details->deadline = calld->deadline;
      r->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
      r->data.recv_metadata = rc->data.batch.initial_metadata;
      r->flags = 0;
      r++;
      publish = publish_registered_or_batch;
      break;
    case REGISTERED_CALL:
      *rc->data.registered.deadline = calld->deadline;
      r->op = GRPC_IOREQ_RECV_INITIAL_METADATA;
      r->data.recv_metadata = rc->data.registered.initial_metadata;
      r->flags = 0;
      r++;
      if (rc->data.registered.optional_payload) {
        r->op = GRPC_IOREQ_RECV_MESSAGE;
        r->data.recv_message = rc->data.registered.optional_payload;
        r->flags = 0;
        r++;
      }
      publish = publish_registered_or_batch;
      break;
  }

  GRPC_CALL_INTERNAL_REF(calld->call, "server");
  grpc_call_start_ioreq_and_call_back(calld->call, req, r - req, publish,
                                      rc->tag);
}

static void fail_call(grpc_server *server, requested_call *rc) {
  *rc->call = NULL;
  switch (rc->type) {
    case BATCH_CALL:
      rc->data.batch.initial_metadata->count = 0;
      break;
    case REGISTERED_CALL:
      rc->data.registered.initial_metadata->count = 0;
      break;
  }
  grpc_cq_end_op(rc->cq_for_notification, rc->tag, NULL, 0);
}

static void publish_registered_or_batch(grpc_call *call, int success,
                                        void *tag) {
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  grpc_cq_end_op(calld->cq_new, tag, call, success);
}

const grpc_channel_args *grpc_server_get_channel_args(grpc_server *server) {
  return server->channel_args;
}

int grpc_server_has_open_connections(grpc_server *server) {
  int r;
  gpr_mu_lock(&server->mu_global);
  r = server->root_channel_data.next != &server->root_channel_data;
  gpr_mu_unlock(&server->mu_global);
  return r;
}
