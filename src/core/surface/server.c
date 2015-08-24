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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/channel/census_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/support/stack_lockfree.h"
#include "src/core/support/string.h"
#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"
#include "src/core/surface/init.h"
#include "src/core/transport/metadata.h"

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

typedef struct requested_call {
  requested_call_type type;
  void *tag;
  grpc_server *server;
  grpc_completion_queue *cq_bound_to_call;
  grpc_completion_queue *cq_for_notification;
  grpc_call **call;
  grpc_cq_completion completion;
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

typedef struct channel_registered_method {
  registered_method *server_registered_method;
  grpc_mdstr *method;
  grpc_mdstr *host;
} channel_registered_method;

struct channel_data {
  grpc_server *server;
  grpc_connectivity_state connectivity_state;
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
  grpc_iomgr_closure channel_connectivity_changed;
};

typedef struct shutdown_tag {
  void *tag;
  grpc_completion_queue *cq;
  grpc_cq_completion completion;
} shutdown_tag;

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

typedef struct request_matcher request_matcher;

struct call_data {
  grpc_call *call;

  /** protects state */
  gpr_mu mu_state;
  /** the current state of a call - see call_state */
  call_state state;

  grpc_mdstr *path;
  grpc_mdstr *host;
  gpr_timespec deadline;
  int got_initial_metadata;

  grpc_completion_queue *cq_new;

  grpc_stream_op_buffer *recv_ops;
  grpc_stream_state *recv_state;
  grpc_iomgr_closure *on_done_recv;

  grpc_iomgr_closure server_on_recv;
  grpc_iomgr_closure kill_zombie_closure;

  call_data *pending_next;
};

struct request_matcher {
  call_data *pending_head;
  call_data *pending_tail;
  gpr_stack_lockfree *requests;
};

struct registered_method {
  char *method;
  char *host;
  request_matcher request_matcher;
  registered_method *next;
};

typedef struct {
  grpc_channel **channels;
  size_t num_channels;
} channel_broadcaster;

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
  gpr_mu mu_call;   /* mutex for call-specific state */

  registered_method *registered_methods;
  request_matcher unregistered_request_matcher;
  /** free list of available requested_calls indices */
  gpr_stack_lockfree *request_freelist;
  /** requested call backing data */
  requested_call *requested_calls;
  int max_requested_calls;

  gpr_atm shutdown_flag;
  gpr_uint8 shutdown_published;
  size_t num_shutdown_tags;
  shutdown_tag *shutdown_tags;

  channel_data root_channel_data;

  listener *listeners;
  int listeners_destroyed;
  gpr_refcount internal_refcount;

  /** when did we print the last shutdown progress message */
  gpr_timespec last_shutdown_message_time;
};

#define SERVER_FROM_CALL_ELEM(elem) \
  (((channel_data *)(elem)->channel_data)->server)

static void begin_call(grpc_server *server, call_data *calld,
                       requested_call *rc);
static void fail_call(grpc_server *server, requested_call *rc);
/* Before calling maybe_finish_shutdown, we must hold mu_global and not
   hold mu_call */
static void maybe_finish_shutdown(grpc_server *server);

/*
 * channel broadcaster
 */

/* assumes server locked */
static void channel_broadcaster_init(grpc_server *s, channel_broadcaster *cb) {
  channel_data *c;
  size_t count = 0;
  for (c = s->root_channel_data.next; c != &s->root_channel_data; c = c->next) {
    count++;
  }
  cb->num_channels = count;
  cb->channels = gpr_malloc(sizeof(*cb->channels) * cb->num_channels);
  count = 0;
  for (c = s->root_channel_data.next; c != &s->root_channel_data; c = c->next) {
    cb->channels[count++] = c->channel;
    GRPC_CHANNEL_INTERNAL_REF(c->channel, "broadcast");
  }
}

struct shutdown_cleanup_args {
  grpc_iomgr_closure closure;
  gpr_slice slice;
};

static void shutdown_cleanup(void *arg, int iomgr_status_ignored) {
  struct shutdown_cleanup_args *a = arg;
  gpr_slice_unref(a->slice);
  gpr_free(a);
}

static void send_shutdown(grpc_channel *channel, int send_goaway,
                          int send_disconnect) {
  grpc_transport_op op;
  struct shutdown_cleanup_args *sc;
  grpc_channel_element *elem;

  memset(&op, 0, sizeof(op));
  op.send_goaway = send_goaway;
  sc = gpr_malloc(sizeof(*sc));
  sc->slice = gpr_slice_from_copied_string("Server shutdown");
  op.goaway_message = &sc->slice;
  op.goaway_status = GRPC_STATUS_OK;
  op.disconnect = send_disconnect;
  grpc_iomgr_closure_init(&sc->closure, shutdown_cleanup, sc);
  op.on_consumed = &sc->closure;

  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  elem->filter->start_transport_op(elem, &op);
}

static void channel_broadcaster_shutdown(channel_broadcaster *cb,
                                         int send_goaway,
                                         int force_disconnect) {
  size_t i;

  for (i = 0; i < cb->num_channels; i++) {
    send_shutdown(cb->channels[i], send_goaway, force_disconnect);
    GRPC_CHANNEL_INTERNAL_UNREF(cb->channels[i], "broadcast");
  }
  gpr_free(cb->channels);
}

/*
 * request_matcher
 */

static void request_matcher_init(request_matcher *request_matcher,
                                 int entries) {
  memset(request_matcher, 0, sizeof(*request_matcher));
  request_matcher->requests = gpr_stack_lockfree_create(entries);
}

static void request_matcher_destroy(request_matcher *request_matcher) {
  GPR_ASSERT(gpr_stack_lockfree_pop(request_matcher->requests) == -1);
  gpr_stack_lockfree_destroy(request_matcher->requests);
}

static void kill_zombie(void *elem, int success) {
  grpc_call_destroy(grpc_call_from_top_element(elem));
}

static void request_matcher_zombify_all_pending_calls(
    request_matcher *request_matcher) {
  while (request_matcher->pending_head) {
    call_data *calld = request_matcher->pending_head;
    request_matcher->pending_head = calld->pending_next;
    gpr_mu_lock(&calld->mu_state);
    calld->state = ZOMBIED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_iomgr_closure_init(
        &calld->kill_zombie_closure, kill_zombie,
        grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0));
    grpc_iomgr_add_callback(&calld->kill_zombie_closure);
  }
}

static void request_matcher_kill_requests(grpc_server *server,
                                          request_matcher *rm) {
  int request_id;
  while ((request_id = gpr_stack_lockfree_pop(rm->requests)) != -1) {
    fail_call(server, &server->requested_calls[request_id]);
  }
}

/*
 * server proper
 */

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
  while ((rm = server->registered_methods) != NULL) {
    server->registered_methods = rm->next;
    request_matcher_destroy(&rm->request_matcher);
    gpr_free(rm->method);
    gpr_free(rm->host);
    gpr_free(rm);
  }
  for (i = 0; i < server->cq_count; i++) {
    GRPC_CQ_INTERNAL_UNREF(server->cqs[i], "server");
  }
  request_matcher_destroy(&server->unregistered_request_matcher);
  gpr_stack_lockfree_destroy(server->request_freelist);
  gpr_free(server->cqs);
  gpr_free(server->pollsets);
  gpr_free(server->shutdown_tags);
  gpr_free(server->requested_calls);
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

static void finish_start_new_rpc(grpc_server *server, grpc_call_element *elem,
                                 request_matcher *request_matcher) {
  call_data *calld = elem->call_data;
  int request_id;

  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    gpr_mu_lock(&calld->mu_state);
    calld->state = ZOMBIED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
    grpc_iomgr_add_callback(&calld->kill_zombie_closure);
    return;
  }

  request_id = gpr_stack_lockfree_pop(request_matcher->requests);
  if (request_id == -1) {
    gpr_mu_lock(&server->mu_call);
    gpr_mu_lock(&calld->mu_state);
    calld->state = PENDING;
    gpr_mu_unlock(&calld->mu_state);
    if (request_matcher->pending_head == NULL) {
      request_matcher->pending_tail = request_matcher->pending_head = calld;
    } else {
      request_matcher->pending_tail->pending_next = calld;
      request_matcher->pending_tail = calld;
    }
    calld->pending_next = NULL;
    gpr_mu_unlock(&server->mu_call);
  } else {
    gpr_mu_lock(&calld->mu_state);
    calld->state = ACTIVATED;
    gpr_mu_unlock(&calld->mu_state);
    begin_call(server, calld, &server->requested_calls[request_id]);
  }
}

static void start_new_rpc(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  grpc_server *server = chand->server;
  gpr_uint32 i;
  gpr_uint32 hash;
  channel_registered_method *rm;

  if (chand->registered_methods && calld->path && calld->host) {
    /* TODO(ctiller): unify these two searches */
    /* check for an exact match with host */
    hash = GRPC_MDSTR_KV_HASH(calld->host->hash, calld->path->hash);
    for (i = 0; i <= chand->registered_method_max_probes; i++) {
      rm = &chand->registered_methods[(hash + i) %
                                      chand->registered_method_slots];
      if (!rm) break;
      if (rm->host != calld->host) continue;
      if (rm->method != calld->path) continue;
      finish_start_new_rpc(server, elem,
                           &rm->server_registered_method->request_matcher);
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
      finish_start_new_rpc(server, elem,
                           &rm->server_registered_method->request_matcher);
      return;
    }
  }
  finish_start_new_rpc(server, elem, &server->unregistered_request_matcher);
}

static int num_listeners(grpc_server *server) {
  listener *l;
  int n = 0;
  for (l = server->listeners; l; l = l->next) {
    n++;
  }
  return n;
}

static void done_shutdown_event(void *server, grpc_cq_completion *completion) {
  server_unref(server);
}

static int num_channels(grpc_server *server) {
  channel_data *chand;
  int n = 0;
  for (chand = server->root_channel_data.next;
       chand != &server->root_channel_data; chand = chand->next) {
    n++;
  }
  return n;
}

static void kill_pending_work_locked(grpc_server *server) {
  registered_method *rm;
  request_matcher_kill_requests(server, &server->unregistered_request_matcher);
  request_matcher_zombify_all_pending_calls(
      &server->unregistered_request_matcher);
  for (rm = server->registered_methods; rm; rm = rm->next) {
    request_matcher_kill_requests(server, &rm->request_matcher);
    request_matcher_zombify_all_pending_calls(&rm->request_matcher);
  }
}

static void maybe_finish_shutdown(grpc_server *server) {
  size_t i;
  if (!gpr_atm_acq_load(&server->shutdown_flag) || server->shutdown_published) {
    return;
  }

  kill_pending_work_locked(server);

  if (server->root_channel_data.next != &server->root_channel_data ||
      server->listeners_destroyed < num_listeners(server)) {
    if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME),
                                  server->last_shutdown_message_time),
                     gpr_time_from_seconds(1, GPR_TIMESPAN)) >= 0) {
      server->last_shutdown_message_time = gpr_now(GPR_CLOCK_REALTIME);
      gpr_log(GPR_DEBUG,
              "Waiting for %d channels and %d/%d listeners to be destroyed"
              " before shutting down server",
              num_channels(server),
              num_listeners(server) - server->listeners_destroyed,
              num_listeners(server));
    }
    return;
  }
  server->shutdown_published = 1;
  for (i = 0; i < server->num_shutdown_tags; i++) {
    server_ref(server);
    grpc_cq_end_op(server->shutdown_tags[i].cq, server->shutdown_tags[i].tag, 1,
                   done_shutdown_event, server,
                   &server->shutdown_tags[i].completion);
  }
}

static grpc_mdelem *server_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (md->key == chand->path_key) {
    calld->path = GRPC_MDSTR_REF(md->value);
    return NULL;
  } else if (md->key == chand->authority_key) {
    calld->host = GRPC_MDSTR_REF(md->value);
    return NULL;
  }
  return md;
}

static void server_on_recv(void *ptr, int success) {
  grpc_call_element *elem = ptr;
  call_data *calld = elem->call_data;
  gpr_timespec op_deadline;

  if (success && !calld->got_initial_metadata) {
    size_t i;
    size_t nops = calld->recv_ops->nops;
    grpc_stream_op *ops = calld->recv_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *op = &ops[i];
      if (op->type != GRPC_OP_METADATA) continue;
      grpc_metadata_batch_filter(&op->data.metadata, server_filter, elem);
      op_deadline = op->data.metadata.deadline;
      if (0 !=
          gpr_time_cmp(op_deadline, gpr_inf_future(op_deadline.clock_type))) {
        calld->deadline = op->data.metadata.deadline;
      }
      if (calld->host && calld->path) {
        calld->got_initial_metadata = 1;
        start_new_rpc(elem);
      }
      break;
    }
  }

  switch (*calld->recv_state) {
    case GRPC_STREAM_OPEN:
      break;
    case GRPC_STREAM_SEND_CLOSED:
      break;
    case GRPC_STREAM_RECV_CLOSED:
      gpr_mu_lock(&calld->mu_state);
      if (calld->state == NOT_STARTED) {
        calld->state = ZOMBIED;
        gpr_mu_unlock(&calld->mu_state);
        grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      } else {
        gpr_mu_unlock(&calld->mu_state);
      }
      break;
    case GRPC_STREAM_CLOSED:
      gpr_mu_lock(&calld->mu_state);
      if (calld->state == NOT_STARTED) {
        calld->state = ZOMBIED;
        gpr_mu_unlock(&calld->mu_state);
        grpc_iomgr_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      } else if (calld->state == PENDING) {
        calld->state = ZOMBIED;
        gpr_mu_unlock(&calld->mu_state);
        /* zombied call will be destroyed when it's removed from the pending
           queue... later */
      } else {
        gpr_mu_unlock(&calld->mu_state);
      }
      break;
  }

  calld->on_done_recv->cb(calld->on_done_recv->cb_arg, success);
}

static void server_mutate_op(grpc_call_element *elem,
                             grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;

  if (op->recv_ops) {
    /* substitute our callback for the higher callback */
    calld->recv_ops = op->recv_ops;
    calld->recv_state = op->recv_state;
    calld->on_done_recv = op->on_done_recv;
    op->on_done_recv = &calld->server_on_recv;
  }
}

static void server_start_transport_stream_op(grpc_call_element *elem,
                                             grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  server_mutate_op(elem, op);
  grpc_call_next_op(elem, op);
}

static void accept_stream(void *cd, grpc_transport *transport,
                          const void *transport_server_data) {
  channel_data *chand = cd;
  /* create a call */
  grpc_call_create(chand->channel, NULL, 0, NULL, transport_server_data, NULL,
                   0, gpr_inf_future(GPR_CLOCK_MONOTONIC));
}

static void channel_connectivity_changed(void *cd, int iomgr_status_ignored) {
  channel_data *chand = cd;
  grpc_server *server = chand->server;
  if (chand->connectivity_state != GRPC_CHANNEL_FATAL_FAILURE) {
    grpc_transport_op op;
    memset(&op, 0, sizeof(op));
    op.on_connectivity_state_change = &chand->channel_connectivity_changed,
    op.connectivity_state = &chand->connectivity_state;
    grpc_channel_next_op(grpc_channel_stack_element(
                             grpc_channel_get_channel_stack(chand->channel), 0),
                         &op);
  } else {
    gpr_mu_lock(&server->mu_global);
    destroy_channel(chand);
    gpr_mu_unlock(&server->mu_global);
    GRPC_CHANNEL_INTERNAL_UNREF(chand->channel, "connectivity");
  }
}

static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  memset(calld, 0, sizeof(call_data));
  calld->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  calld->call = grpc_call_from_top_element(elem);
  gpr_mu_init(&calld->mu_state);

  grpc_iomgr_closure_init(&calld->server_on_recv, server_on_recv, elem);

  server_ref(chand->server);

  if (initial_op) server_mutate_op(elem, initial_op);
}

static void destroy_call_elem(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;

  GPR_ASSERT(calld->state != PENDING);

  if (calld->host) {
    GRPC_MDSTR_UNREF(calld->host);
  }
  if (calld->path) {
    GRPC_MDSTR_UNREF(calld->path);
  }

  gpr_mu_destroy(&calld->mu_state);

  server_unref(chand->server);
}

static void init_channel_elem(grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
  chand->server = NULL;
  chand->channel = NULL;
  chand->path_key = grpc_mdstr_from_string(metadata_context, ":path", 0);
  chand->authority_key =
      grpc_mdstr_from_string(metadata_context, ":authority", 0);
  chand->next = chand->prev = chand;
  chand->registered_methods = NULL;
  chand->connectivity_state = GRPC_CHANNEL_IDLE;
  grpc_iomgr_closure_init(&chand->channel_connectivity_changed,
                          channel_connectivity_changed, chand);
}

static void destroy_channel_elem(grpc_channel_element *elem) {
  size_t i;
  channel_data *chand = elem->channel_data;
  if (chand->registered_methods) {
    for (i = 0; i < chand->registered_method_slots; i++) {
      if (chand->registered_methods[i].method) {
        GRPC_MDSTR_UNREF(chand->registered_methods[i].method);
      }
      if (chand->registered_methods[i].host) {
        GRPC_MDSTR_UNREF(chand->registered_methods[i].host);
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
    GRPC_MDSTR_UNREF(chand->path_key);
    GRPC_MDSTR_UNREF(chand->authority_key);
    server_unref(chand->server);
  }
}

static const grpc_channel_filter server_surface_filter = {
    server_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "server",
};

void grpc_server_register_completion_queue(grpc_server *server,
                                           grpc_completion_queue *cq,
                                           void *reserved) {
  size_t i, n;
  GPR_ASSERT(!reserved);
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

grpc_server *grpc_server_create_from_filters(
    const grpc_channel_filter **filters, size_t filter_count,
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

  /* TODO(ctiller): expose a channel_arg for this */
  server->max_requested_calls = 32768;
  server->request_freelist =
      gpr_stack_lockfree_create(server->max_requested_calls);
  for (i = 0; i < (size_t)server->max_requested_calls; i++) {
    gpr_stack_lockfree_push(server->request_freelist, i);
  }
  request_matcher_init(&server->unregistered_request_matcher,
                       server->max_requested_calls);
  server->requested_calls = gpr_malloc(server->max_requested_calls *
                                       sizeof(*server->requested_calls));

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
  request_matcher_init(&m->request_matcher, server->max_requested_calls);
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

void grpc_server_setup_transport(grpc_server *s, grpc_transport *transport,
                                 grpc_channel_filter const **extra_filters,
                                 size_t num_extra_filters, grpc_mdctx *mdctx,
                                 const grpc_channel_args *args) {
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
  grpc_transport_op op;

  for (i = 0; i < s->channel_filter_count; i++) {
    filters[i] = s->channel_filters[i];
  }
  for (; i < s->channel_filter_count + num_extra_filters; i++) {
    filters[i] = extra_filters[i - s->channel_filter_count];
  }
  filters[i] = &grpc_connected_channel_filter;

  for (i = 0; i < s->cq_count; i++) {
    memset(&op, 0, sizeof(op));
    op.bind_pollset = grpc_cq_pollset(s->cqs[i]);
    grpc_transport_perform_op(transport, &op);
  }

  channel = grpc_channel_create_from_filters(NULL, filters, num_filters, args,
                                             mdctx, 0);
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
      host = rm->host ? grpc_mdstr_from_string(mdctx, rm->host, 0) : NULL;
      method = grpc_mdstr_from_string(mdctx, rm->method, 0);
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

  grpc_connected_channel_bind_transport(grpc_channel_get_channel_stack(channel),
                                        transport);

  gpr_mu_lock(&s->mu_global);
  chand->next = &s->root_channel_data;
  chand->prev = chand->next->prev;
  chand->next->prev = chand->prev->next = chand;
  gpr_mu_unlock(&s->mu_global);

  gpr_free(filters);

  GRPC_CHANNEL_INTERNAL_REF(channel, "connectivity");
  memset(&op, 0, sizeof(op));
  op.set_accept_stream = accept_stream;
  op.set_accept_stream_user_data = chand;
  op.on_connectivity_state_change = &chand->channel_connectivity_changed;
  op.connectivity_state = &chand->connectivity_state;
  op.disconnect = gpr_atm_acq_load(&s->shutdown_flag);
  grpc_transport_perform_op(transport, &op);
}

void done_published_shutdown(void *done_arg, grpc_cq_completion *storage) {
  (void) done_arg;
  gpr_free(storage);
}

void grpc_server_shutdown_and_notify(grpc_server *server,
                                     grpc_completion_queue *cq, void *tag) {
  listener *l;
  shutdown_tag *sdt;
  channel_broadcaster broadcaster;

  GRPC_SERVER_LOG_SHUTDOWN(GPR_INFO, server, cq, tag);

  /* lock, and gather up some stuff to do */
  gpr_mu_lock(&server->mu_global);
  grpc_cq_begin_op(cq);
  if (server->shutdown_published) {
    grpc_cq_end_op(cq, tag, 1, done_published_shutdown, NULL,
                   gpr_malloc(sizeof(grpc_cq_completion)));
    gpr_mu_unlock(&server->mu_global);
    return;
  }
  server->shutdown_tags =
      gpr_realloc(server->shutdown_tags,
                  sizeof(shutdown_tag) * (server->num_shutdown_tags + 1));
  sdt = &server->shutdown_tags[server->num_shutdown_tags++];
  sdt->tag = tag;
  sdt->cq = cq;
  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    gpr_mu_unlock(&server->mu_global);
    return;
  }

  server->last_shutdown_message_time = gpr_now(GPR_CLOCK_REALTIME);

  channel_broadcaster_init(server, &broadcaster);

  /* collect all unregistered then registered calls */
  gpr_mu_lock(&server->mu_call);
  kill_pending_work_locked(server);
  gpr_mu_unlock(&server->mu_call);

  gpr_atm_rel_store(&server->shutdown_flag, 1);
  maybe_finish_shutdown(server);
  gpr_mu_unlock(&server->mu_global);

  /* Shutdown listeners */
  for (l = server->listeners; l; l = l->next) {
    l->destroy(server, l->arg);
  }

  channel_broadcaster_shutdown(&broadcaster, 1, 0);
}

void grpc_server_listener_destroy_done(void *s) {
  grpc_server *server = s;
  gpr_mu_lock(&server->mu_global);
  server->listeners_destroyed++;
  maybe_finish_shutdown(server);
  gpr_mu_unlock(&server->mu_global);
}

void grpc_server_cancel_all_calls(grpc_server *server) {
  channel_broadcaster broadcaster;

  gpr_mu_lock(&server->mu_global);
  channel_broadcaster_init(server, &broadcaster);
  gpr_mu_unlock(&server->mu_global);

  channel_broadcaster_shutdown(&broadcaster, 0, 1);
}

void grpc_server_destroy(grpc_server *server) {
  listener *l;

  gpr_mu_lock(&server->mu_global);
  GPR_ASSERT(gpr_atm_acq_load(&server->shutdown_flag) || !server->listeners);
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
  request_matcher *request_matcher = NULL;
  int request_id;
  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    fail_call(server, rc);
    return GRPC_CALL_OK;
  }
  request_id = gpr_stack_lockfree_pop(server->request_freelist);
  if (request_id == -1) {
    /* out of request ids: just fail this one */
    fail_call(server, rc);
    return GRPC_CALL_OK;
  }
  switch (rc->type) {
    case BATCH_CALL:
      request_matcher = &server->unregistered_request_matcher;
      break;
    case REGISTERED_CALL:
      request_matcher = &rc->data.registered.registered_method->request_matcher;
      break;
  }
  server->requested_calls[request_id] = *rc;
  gpr_free(rc);
  if (gpr_stack_lockfree_push(request_matcher->requests, request_id)) {
    /* this was the first queued request: we need to lock and start
       matching calls */
    gpr_mu_lock(&server->mu_call);
    while ((calld = request_matcher->pending_head) != NULL) {
      request_id = gpr_stack_lockfree_pop(request_matcher->requests);
      if (request_id == -1) break;
      request_matcher->pending_head = calld->pending_next;
      gpr_mu_unlock(&server->mu_call);
      gpr_mu_lock(&calld->mu_state);
      if (calld->state == ZOMBIED) {
        gpr_mu_unlock(&calld->mu_state);
        grpc_iomgr_closure_init(
            &calld->kill_zombie_closure, kill_zombie,
            grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0));
        grpc_iomgr_add_callback(&calld->kill_zombie_closure);
      } else {
        GPR_ASSERT(calld->state == PENDING);
        calld->state = ACTIVATED;
        gpr_mu_unlock(&calld->mu_state);
        begin_call(server, calld, &server->requested_calls[request_id]);
      }
      gpr_mu_lock(&server->mu_call);
    }
    gpr_mu_unlock(&server->mu_call);
  }
  return GRPC_CALL_OK;
}

grpc_call_error grpc_server_request_call(
    grpc_server *server, grpc_call **call, grpc_call_details *details,
    grpc_metadata_array *initial_metadata,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag) {
  requested_call *rc = gpr_malloc(sizeof(*rc));
  GRPC_SERVER_LOG_REQUEST_CALL(GPR_INFO, server, call, details,
                               initial_metadata, cq_bound_to_call,
                               cq_for_notification, tag);
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    gpr_free(rc);
    return GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
  }
  grpc_cq_begin_op(cq_for_notification);
  details->reserved = NULL;
  rc->type = BATCH_CALL;
  rc->server = server;
  rc->tag = tag;
  rc->cq_bound_to_call = cq_bound_to_call;
  rc->cq_for_notification = cq_for_notification;
  rc->call = call;
  rc->data.batch.details = details;
  rc->data.batch.initial_metadata = initial_metadata;
  return queue_call_request(server, rc);
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *rm, grpc_call **call, gpr_timespec *deadline,
    grpc_metadata_array *initial_metadata, grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag) {
  requested_call *rc = gpr_malloc(sizeof(*rc));
  registered_method *registered_method = rm;
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    gpr_free(rc);
    return GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
  }
  grpc_cq_begin_op(cq_for_notification);
  rc->type = REGISTERED_CALL;
  rc->server = server;
  rc->tag = tag;
  rc->cq_bound_to_call = cq_bound_to_call;
  rc->cq_for_notification = cq_for_notification;
  rc->call = call;
  rc->data.registered.registered_method = registered_method;
  rc->data.registered.deadline = deadline;
  rc->data.registered.initial_metadata = initial_metadata;
  rc->data.registered.optional_payload = optional_payload;
  return queue_call_request(server, rc);
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
      GPR_ASSERT(calld->host != NULL);
      GPR_ASSERT(calld->path != NULL);
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
  grpc_call_start_ioreq_and_call_back(calld->call, req, r - req, publish, rc);
}

static void done_request_event(void *req, grpc_cq_completion *c) {
  requested_call *rc = req;
  grpc_server *server = rc->server;

  if (rc >= server->requested_calls &&
      rc < server->requested_calls + server->max_requested_calls) {
    gpr_stack_lockfree_push(server->request_freelist,
                            rc - server->requested_calls);
  } else {
    gpr_free(req);
  }

  server_unref(server);
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
  server_ref(server);
  grpc_cq_end_op(rc->cq_for_notification, rc->tag, 0, done_request_event, rc,
                 &rc->completion);
}

static void publish_registered_or_batch(grpc_call *call, int success,
                                        void *prc) {
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  requested_call *rc = prc;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  server_ref(chand->server);
  grpc_cq_end_op(calld->cq_new, rc->tag, success, done_request_event, rc,
                 &rc->completion);
  GRPC_CALL_INTERNAL_UNREF(call, "server", 0);
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
