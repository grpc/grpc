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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/census/grpc_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/support/stack_lockfree.h"
#include "src/core/support/string.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"
#include "src/core/surface/init.h"
#include "src/core/transport/metadata.h"
#include "src/core/transport/static_metadata.h"

typedef struct listener {
  void *arg;
  void (*start)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                grpc_pollset **pollsets, size_t pollset_count);
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                  grpc_closure *closure);
  struct listener *next;
  grpc_closure destroy_done;
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
  grpc_metadata_array *initial_metadata;
  union {
    struct {
      grpc_call_details *details;
    } batch;
    struct {
      registered_method *registered_method;
      gpr_timespec *deadline;
      grpc_byte_buffer **optional_payload;
    } registered;
  } data;
  grpc_closure publish;
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
  /* linked list of all channels on a server */
  channel_data *next;
  channel_data *prev;
  channel_registered_method *registered_methods;
  gpr_uint32 registered_method_slots;
  gpr_uint32 registered_method_max_probes;
  grpc_closure finish_destroy_channel_closure;
  grpc_closure channel_connectivity_changed;
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

  grpc_completion_queue *cq_new;

  grpc_metadata_batch *recv_initial_metadata;
  grpc_metadata_array initial_metadata;

  grpc_closure got_initial_metadata;
  grpc_closure server_on_recv_initial_metadata;
  grpc_closure kill_zombie_closure;
  grpc_closure *on_done_recv_initial_metadata;

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
  grpc_channel_filter const **channel_filters;
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
  size_t max_requested_calls;

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

static void begin_call(grpc_exec_ctx *exec_ctx, grpc_server *server,
                       call_data *calld, requested_call *rc);
static void fail_call(grpc_exec_ctx *exec_ctx, grpc_server *server,
                      requested_call *rc);
/* Before calling maybe_finish_shutdown, we must hold mu_global and not
   hold mu_call */
static void maybe_finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_server *server);

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
  grpc_closure closure;
  gpr_slice slice;
};

static void shutdown_cleanup(grpc_exec_ctx *exec_ctx, void *arg,
                             int iomgr_status_ignored) {
  struct shutdown_cleanup_args *a = arg;
  gpr_slice_unref(a->slice);
  gpr_free(a);
}

static void send_shutdown(grpc_exec_ctx *exec_ctx, grpc_channel *channel,
                          int send_goaway, int send_disconnect) {
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
  grpc_closure_init(&sc->closure, shutdown_cleanup, sc);
  op.on_consumed = &sc->closure;

  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  elem->filter->start_transport_op(exec_ctx, elem, &op);
}

static void channel_broadcaster_shutdown(grpc_exec_ctx *exec_ctx,
                                         channel_broadcaster *cb,
                                         int send_goaway,
                                         int force_disconnect) {
  size_t i;

  for (i = 0; i < cb->num_channels; i++) {
    send_shutdown(exec_ctx, cb->channels[i], send_goaway, force_disconnect);
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, cb->channels[i], "broadcast");
  }
  gpr_free(cb->channels);
}

/*
 * request_matcher
 */

static void request_matcher_init(request_matcher *rm, size_t entries) {
  memset(rm, 0, sizeof(*rm));
  rm->requests = gpr_stack_lockfree_create(entries);
}

static void request_matcher_destroy(request_matcher *rm) {
  GPR_ASSERT(gpr_stack_lockfree_pop(rm->requests) == -1);
  gpr_stack_lockfree_destroy(rm->requests);
}

static void kill_zombie(grpc_exec_ctx *exec_ctx, void *elem, int success) {
  grpc_call_destroy(grpc_call_from_top_element(elem));
}

static void request_matcher_zombify_all_pending_calls(grpc_exec_ctx *exec_ctx,
                                                      request_matcher *rm) {
  while (rm->pending_head) {
    call_data *calld = rm->pending_head;
    rm->pending_head = calld->pending_next;
    gpr_mu_lock(&calld->mu_state);
    calld->state = ZOMBIED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_closure_init(
        &calld->kill_zombie_closure, kill_zombie,
        grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0));
    grpc_exec_ctx_enqueue(exec_ctx, &calld->kill_zombie_closure, 1);
  }
}

static void request_matcher_kill_requests(grpc_exec_ctx *exec_ctx,
                                          grpc_server *server,
                                          request_matcher *rm) {
  int request_id;
  while ((request_id = gpr_stack_lockfree_pop(rm->requests)) != -1) {
    fail_call(exec_ctx, server, &server->requested_calls[request_id]);
  }
}

/*
 * server proper
 */

static void server_ref(grpc_server *server) {
  gpr_ref(&server->internal_refcount);
}

static void server_delete(grpc_exec_ctx *exec_ctx, grpc_server *server) {
  registered_method *rm;
  size_t i;
  grpc_channel_args_destroy(server->channel_args);
  gpr_mu_destroy(&server->mu_global);
  gpr_mu_destroy(&server->mu_call);
  gpr_free((void *)server->channel_filters);
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

static void server_unref(grpc_exec_ctx *exec_ctx, grpc_server *server) {
  if (gpr_unref(&server->internal_refcount)) {
    server_delete(exec_ctx, server);
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

static void finish_destroy_channel(grpc_exec_ctx *exec_ctx, void *cd,
                                   int success) {
  channel_data *chand = cd;
  grpc_server *server = chand->server;
  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, chand->channel, "server");
  server_unref(exec_ctx, server);
}

static void destroy_channel(grpc_exec_ctx *exec_ctx, channel_data *chand) {
  if (is_channel_orphaned(chand)) return;
  GPR_ASSERT(chand->server != NULL);
  orphan_channel(chand);
  server_ref(chand->server);
  maybe_finish_shutdown(exec_ctx, chand->server);
  chand->finish_destroy_channel_closure.cb = finish_destroy_channel;
  chand->finish_destroy_channel_closure.cb_arg = chand;
  grpc_exec_ctx_enqueue(exec_ctx, &chand->finish_destroy_channel_closure, 1);
}

static void finish_start_new_rpc(grpc_exec_ctx *exec_ctx, grpc_server *server,
                                 grpc_call_element *elem, request_matcher *rm) {
  call_data *calld = elem->call_data;
  int request_id;

  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    gpr_mu_lock(&calld->mu_state);
    calld->state = ZOMBIED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
    grpc_exec_ctx_enqueue(exec_ctx, &calld->kill_zombie_closure, 1);
    return;
  }

  request_id = gpr_stack_lockfree_pop(rm->requests);
  if (request_id == -1) {
    gpr_mu_lock(&server->mu_call);
    gpr_mu_lock(&calld->mu_state);
    calld->state = PENDING;
    gpr_mu_unlock(&calld->mu_state);
    if (rm->pending_head == NULL) {
      rm->pending_tail = rm->pending_head = calld;
    } else {
      rm->pending_tail->pending_next = calld;
      rm->pending_tail = calld;
    }
    calld->pending_next = NULL;
    gpr_mu_unlock(&server->mu_call);
  } else {
    gpr_mu_lock(&calld->mu_state);
    calld->state = ACTIVATED;
    gpr_mu_unlock(&calld->mu_state);
    begin_call(exec_ctx, server, calld, &server->requested_calls[request_id]);
  }
}

static void start_new_rpc(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
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
      finish_start_new_rpc(exec_ctx, server, elem,
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
      finish_start_new_rpc(exec_ctx, server, elem,
                           &rm->server_registered_method->request_matcher);
      return;
    }
  }
  finish_start_new_rpc(exec_ctx, server, elem,
                       &server->unregistered_request_matcher);
}

static int num_listeners(grpc_server *server) {
  listener *l;
  int n = 0;
  for (l = server->listeners; l; l = l->next) {
    n++;
  }
  return n;
}

static void done_shutdown_event(grpc_exec_ctx *exec_ctx, void *server,
                                grpc_cq_completion *completion) {
  server_unref(exec_ctx, server);
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

static void kill_pending_work_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_server *server) {
  registered_method *rm;
  request_matcher_kill_requests(exec_ctx, server,
                                &server->unregistered_request_matcher);
  request_matcher_zombify_all_pending_calls(
      exec_ctx, &server->unregistered_request_matcher);
  for (rm = server->registered_methods; rm; rm = rm->next) {
    request_matcher_kill_requests(exec_ctx, server, &rm->request_matcher);
    request_matcher_zombify_all_pending_calls(exec_ctx, &rm->request_matcher);
  }
}

static void maybe_finish_shutdown(grpc_exec_ctx *exec_ctx,
                                  grpc_server *server) {
  size_t i;
  if (!gpr_atm_acq_load(&server->shutdown_flag) || server->shutdown_published) {
    return;
  }

  kill_pending_work_locked(exec_ctx, server);

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
    grpc_cq_end_op(exec_ctx, server->shutdown_tags[i].cq,
                   server->shutdown_tags[i].tag, 1, done_shutdown_event, server,
                   &server->shutdown_tags[i].completion);
  }
}

static grpc_mdelem *server_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (md->key == GRPC_MDSTR_PATH) {
    calld->path = GRPC_MDSTR_REF(md->value);
    return NULL;
  } else if (md->key == GRPC_MDSTR_AUTHORITY) {
    calld->host = GRPC_MDSTR_REF(md->value);
    return NULL;
  }
  return md;
}

static void server_on_recv_initial_metadata(grpc_exec_ctx *exec_ctx, void *ptr,
                                            int success) {
  grpc_call_element *elem = ptr;
  call_data *calld = elem->call_data;
  gpr_timespec op_deadline;

  grpc_metadata_batch_filter(calld->recv_initial_metadata, server_filter, elem);
  op_deadline = calld->recv_initial_metadata->deadline;
  if (0 != gpr_time_cmp(op_deadline, gpr_inf_future(op_deadline.clock_type))) {
    calld->deadline = op_deadline;
  }
  if (calld->host && calld->path) {
    /* do nothing */
  } else {
    success = 0;
  }

  calld->on_done_recv_initial_metadata->cb(
      exec_ctx, calld->on_done_recv_initial_metadata->cb_arg, success);
}

static void server_mutate_op(grpc_call_element *elem,
                             grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;

  if (op->recv_initial_metadata != NULL) {
    calld->recv_initial_metadata = op->recv_initial_metadata;
    calld->on_done_recv_initial_metadata = op->on_complete;
    op->on_complete = &calld->server_on_recv_initial_metadata;
  }
}

static void server_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                             grpc_call_element *elem,
                                             grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  server_mutate_op(elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
}

static void got_initial_metadata(grpc_exec_ctx *exec_ctx, void *ptr,
                                 int success) {
  grpc_call_element *elem = ptr;
  call_data *calld = elem->call_data;
  if (success) {
    start_new_rpc(exec_ctx, elem);
  } else {
    gpr_mu_lock(&calld->mu_state);
    if (calld->state == NOT_STARTED) {
      calld->state = ZOMBIED;
      gpr_mu_unlock(&calld->mu_state);
      grpc_closure_init(&calld->kill_zombie_closure, kill_zombie, elem);
      grpc_exec_ctx_enqueue(exec_ctx, &calld->kill_zombie_closure, 1);
    } else if (calld->state == PENDING) {
      calld->state = ZOMBIED;
      gpr_mu_unlock(&calld->mu_state);
      /* zombied call will be destroyed when it's removed from the pending
         queue... later */
    } else {
      gpr_mu_unlock(&calld->mu_state);
    }
  }
}

static void accept_stream(grpc_exec_ctx *exec_ctx, void *cd,
                          grpc_transport *transport,
                          const void *transport_server_data) {
  channel_data *chand = cd;
  /* create a call */
  grpc_call *call =
      grpc_call_create(chand->channel, NULL, 0, NULL, transport_server_data,
                       NULL, 0, gpr_inf_future(GPR_CLOCK_MONOTONIC));
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.data.recv_initial_metadata = &calld->initial_metadata;
  grpc_closure_init(&calld->got_initial_metadata, got_initial_metadata, elem);
  grpc_call_start_batch_and_execute(exec_ctx, call, &op, 1,
                                    &calld->got_initial_metadata);
}

static void channel_connectivity_changed(grpc_exec_ctx *exec_ctx, void *cd,
                                         int iomgr_status_ignored) {
  channel_data *chand = cd;
  grpc_server *server = chand->server;
  if (chand->connectivity_state != GRPC_CHANNEL_FATAL_FAILURE) {
    grpc_transport_op op;
    memset(&op, 0, sizeof(op));
    op.on_connectivity_state_change = &chand->channel_connectivity_changed,
    op.connectivity_state = &chand->connectivity_state;
    grpc_channel_next_op(exec_ctx,
                         grpc_channel_stack_element(
                             grpc_channel_get_channel_stack(chand->channel), 0),
                         &op);
  } else {
    gpr_mu_lock(&server->mu_global);
    destroy_channel(exec_ctx, chand);
    gpr_mu_unlock(&server->mu_global);
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, chand->channel, "connectivity");
  }
}

static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  memset(calld, 0, sizeof(call_data));
  calld->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  calld->call = grpc_call_from_top_element(elem);
  gpr_mu_init(&calld->mu_state);

  grpc_closure_init(&calld->server_on_recv_initial_metadata,
                    server_on_recv_initial_metadata, elem);

  server_ref(chand->server);
}

static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;

  GPR_ASSERT(calld->state != PENDING);

  if (calld->host) {
    GRPC_MDSTR_UNREF(calld->host);
  }
  if (calld->path) {
    GRPC_MDSTR_UNREF(calld->path);
  }
  grpc_metadata_array_destroy(&calld->initial_metadata);

  gpr_mu_destroy(&calld->mu_state);

  server_unref(exec_ctx, chand->server);
}

static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(!args->is_last);
  chand->server = NULL;
  chand->channel = NULL;
  chand->next = chand->prev = chand;
  chand->registered_methods = NULL;
  chand->connectivity_state = GRPC_CHANNEL_IDLE;
  grpc_closure_init(&chand->channel_connectivity_changed,
                    channel_connectivity_changed, chand);
}

static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
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
    maybe_finish_shutdown(exec_ctx, chand->server);
    gpr_mu_unlock(&chand->server->mu_global);
    server_unref(exec_ctx, chand->server);
  }
}

static const grpc_channel_filter server_surface_filter = {
    server_start_transport_stream_op, grpc_channel_next_op, sizeof(call_data),
    init_call_elem, grpc_call_stack_ignore_set_pollset, destroy_call_elem,
    sizeof(channel_data), init_channel_elem, destroy_channel_elem,
    grpc_call_next_get_peer, "server",
};

void grpc_server_register_completion_queue(grpc_server *server,
                                           grpc_completion_queue *cq,
                                           void *reserved) {
  size_t i, n;
  GRPC_API_TRACE(
      "grpc_server_register_completion_queue(server=%p, cq=%p, reserved=%p)", 3,
      (server, cq, reserved));
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
    gpr_stack_lockfree_push(server->request_freelist, (int)i);
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
  server->channel_filter_count = filter_count + 1u + (census_enabled ? 1u : 0u);
  server->channel_filters =
      gpr_malloc(server->channel_filter_count * sizeof(grpc_channel_filter *));
  server->channel_filters[0] = &server_surface_filter;
  if (census_enabled) {
    server->channel_filters[1] = &grpc_server_census_filter;
  }
  for (i = 0; i < filter_count; i++) {
    server->channel_filters[i + 1u + (census_enabled ? 1u : 0u)] = filters[i];
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
  GRPC_API_TRACE("grpc_server_register_method(server=%p, method=%s, host=%s)",
                 3, (server, method, host));
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE("grpc_server_start(server=%p)", 1, (server));

  server->pollsets = gpr_malloc(sizeof(grpc_pollset *) * server->cq_count);
  for (i = 0; i < server->cq_count; i++) {
    server->pollsets[i] = grpc_cq_pollset(server->cqs[i]);
  }

  for (l = server->listeners; l; l = l->next) {
    l->start(&exec_ctx, server, l->arg, server->pollsets, server->cq_count);
  }

  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_server_setup_transport(grpc_exec_ctx *exec_ctx, grpc_server *s,
                                 grpc_transport *transport,
                                 grpc_channel_filter const **extra_filters,
                                 size_t num_extra_filters,
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
  size_t slots;
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
    grpc_transport_perform_op(exec_ctx, transport, &op);
  }

  channel = grpc_channel_create_from_filters(exec_ctx, NULL, filters,
                                             num_filters, args, 0);
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
      host = rm->host ? grpc_mdstr_from_string(rm->host) : NULL;
      method = grpc_mdstr_from_string(rm->method);
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
    GPR_ASSERT(slots <= GPR_UINT32_MAX);
    chand->registered_method_slots = (gpr_uint32)slots;
    chand->registered_method_max_probes = max_probes;
  }

  grpc_connected_channel_bind_transport(grpc_channel_get_channel_stack(channel),
                                        transport);

  gpr_mu_lock(&s->mu_global);
  chand->next = &s->root_channel_data;
  chand->prev = chand->next->prev;
  chand->next->prev = chand->prev->next = chand;
  gpr_mu_unlock(&s->mu_global);

  gpr_free((void *)filters);

  GRPC_CHANNEL_INTERNAL_REF(channel, "connectivity");
  memset(&op, 0, sizeof(op));
  op.set_accept_stream = accept_stream;
  op.set_accept_stream_user_data = chand;
  op.on_connectivity_state_change = &chand->channel_connectivity_changed;
  op.connectivity_state = &chand->connectivity_state;
  op.disconnect = gpr_atm_acq_load(&s->shutdown_flag) != 0;
  grpc_transport_perform_op(exec_ctx, transport, &op);
}

void done_published_shutdown(grpc_exec_ctx *exec_ctx, void *done_arg,
                             grpc_cq_completion *storage) {
  (void)done_arg;
  gpr_free(storage);
}

static void listener_destroy_done(grpc_exec_ctx *exec_ctx, void *s,
                                  int success) {
  grpc_server *server = s;
  gpr_mu_lock(&server->mu_global);
  server->listeners_destroyed++;
  maybe_finish_shutdown(exec_ctx, server);
  gpr_mu_unlock(&server->mu_global);
}

void grpc_server_shutdown_and_notify(grpc_server *server,
                                     grpc_completion_queue *cq, void *tag) {
  listener *l;
  shutdown_tag *sdt;
  channel_broadcaster broadcaster;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE("grpc_server_shutdown_and_notify(server=%p, cq=%p, tag=%p)", 3,
                 (server, cq, tag));

  /* lock, and gather up some stuff to do */
  gpr_mu_lock(&server->mu_global);
  grpc_cq_begin_op(cq, tag);
  if (server->shutdown_published) {
    grpc_cq_end_op(&exec_ctx, cq, tag, 1, done_published_shutdown, NULL,
                   gpr_malloc(sizeof(grpc_cq_completion)));
    gpr_mu_unlock(&server->mu_global);
    goto done;
  }
  server->shutdown_tags =
      gpr_realloc(server->shutdown_tags,
                  sizeof(shutdown_tag) * (server->num_shutdown_tags + 1));
  sdt = &server->shutdown_tags[server->num_shutdown_tags++];
  sdt->tag = tag;
  sdt->cq = cq;
  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    gpr_mu_unlock(&server->mu_global);
    goto done;
  }

  server->last_shutdown_message_time = gpr_now(GPR_CLOCK_REALTIME);

  channel_broadcaster_init(server, &broadcaster);

  gpr_atm_rel_store(&server->shutdown_flag, 1);

  /* collect all unregistered then registered calls */
  gpr_mu_lock(&server->mu_call);
  kill_pending_work_locked(&exec_ctx, server);
  gpr_mu_unlock(&server->mu_call);

  maybe_finish_shutdown(&exec_ctx, server);
  gpr_mu_unlock(&server->mu_global);

  /* Shutdown listeners */
  for (l = server->listeners; l; l = l->next) {
    grpc_closure_init(&l->destroy_done, listener_destroy_done, server);
    l->destroy(&exec_ctx, server, l->arg, &l->destroy_done);
  }

  channel_broadcaster_shutdown(&exec_ctx, &broadcaster, 1, 0);

done:
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_server_cancel_all_calls(grpc_server *server) {
  channel_broadcaster broadcaster;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE("grpc_server_cancel_all_calls(server=%p)", 1, (server));

  gpr_mu_lock(&server->mu_global);
  channel_broadcaster_init(server, &broadcaster);
  gpr_mu_unlock(&server->mu_global);

  channel_broadcaster_shutdown(&exec_ctx, &broadcaster, 0, 1);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_server_destroy(grpc_server *server) {
  listener *l;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE("grpc_server_destroy(server=%p)", 1, (server));

  gpr_mu_lock(&server->mu_global);
  GPR_ASSERT(gpr_atm_acq_load(&server->shutdown_flag) || !server->listeners);
  GPR_ASSERT(server->listeners_destroyed == num_listeners(server));

  while (server->listeners) {
    l = server->listeners;
    server->listeners = l->next;
    gpr_free(l);
  }

  gpr_mu_unlock(&server->mu_global);

  server_unref(&exec_ctx, server);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_server_add_listener(
    grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
    void (*start)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                  grpc_pollset **pollsets, size_t pollset_count),
    void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                    grpc_closure *on_done)) {
  listener *l = gpr_malloc(sizeof(listener));
  l->arg = arg;
  l->start = start;
  l->destroy = destroy;
  l->next = server->listeners;
  server->listeners = l;
}

static grpc_call_error queue_call_request(grpc_exec_ctx *exec_ctx,
                                          grpc_server *server,
                                          requested_call *rc) {
  call_data *calld = NULL;
  request_matcher *rm = NULL;
  int request_id;
  if (gpr_atm_acq_load(&server->shutdown_flag)) {
    fail_call(exec_ctx, server, rc);
    return GRPC_CALL_OK;
  }
  request_id = gpr_stack_lockfree_pop(server->request_freelist);
  if (request_id == -1) {
    /* out of request ids: just fail this one */
    fail_call(exec_ctx, server, rc);
    return GRPC_CALL_OK;
  }
  switch (rc->type) {
    case BATCH_CALL:
      rm = &server->unregistered_request_matcher;
      break;
    case REGISTERED_CALL:
      rm = &rc->data.registered.registered_method->request_matcher;
      break;
  }
  server->requested_calls[request_id] = *rc;
  gpr_free(rc);
  if (gpr_stack_lockfree_push(rm->requests, request_id)) {
    /* this was the first queued request: we need to lock and start
       matching calls */
    gpr_mu_lock(&server->mu_call);
    while ((calld = rm->pending_head) != NULL) {
      request_id = gpr_stack_lockfree_pop(rm->requests);
      if (request_id == -1) break;
      rm->pending_head = calld->pending_next;
      gpr_mu_unlock(&server->mu_call);
      gpr_mu_lock(&calld->mu_state);
      if (calld->state == ZOMBIED) {
        gpr_mu_unlock(&calld->mu_state);
        grpc_closure_init(
            &calld->kill_zombie_closure, kill_zombie,
            grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0));
        grpc_exec_ctx_enqueue(exec_ctx, &calld->kill_zombie_closure, 1);
      } else {
        GPR_ASSERT(calld->state == PENDING);
        calld->state = ACTIVATED;
        gpr_mu_unlock(&calld->mu_state);
        begin_call(exec_ctx, server, calld,
                   &server->requested_calls[request_id]);
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
  grpc_call_error error;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  requested_call *rc = gpr_malloc(sizeof(*rc));
  GRPC_API_TRACE(
      "grpc_server_request_call("
      "server=%p, call=%p, details=%p, initial_metadata=%p, "
      "cq_bound_to_call=%p, cq_for_notification=%p, tag=%p)",
      7, (server, call, details, initial_metadata, cq_bound_to_call,
          cq_for_notification, tag));
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    gpr_free(rc);
    error = GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
    goto done;
  }
  grpc_cq_begin_op(cq_for_notification, tag);
  details->reserved = NULL;
  rc->type = BATCH_CALL;
  rc->server = server;
  rc->tag = tag;
  rc->cq_bound_to_call = cq_bound_to_call;
  rc->cq_for_notification = cq_for_notification;
  rc->call = call;
  rc->data.batch.details = details;
  rc->initial_metadata = initial_metadata;
  error = queue_call_request(&exec_ctx, server, rc);
done:
  grpc_exec_ctx_finish(&exec_ctx);
  return error;
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *rmp, grpc_call **call, gpr_timespec *deadline,
    grpc_metadata_array *initial_metadata, grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag) {
  grpc_call_error error;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  requested_call *rc = gpr_malloc(sizeof(*rc));
  registered_method *rm = rmp;
  GRPC_API_TRACE(
      "grpc_server_request_registered_call("
      "server=%p, rmp=%p, call=%p, deadline=%p, initial_metadata=%p, "
      "optional_payload=%p, cq_bound_to_call=%p, cq_for_notification=%p, "
      "tag=%p)",
      9, (server, rmp, call, deadline, initial_metadata, optional_payload,
          cq_bound_to_call, cq_for_notification, tag));
  if (!grpc_cq_is_server_cq(cq_for_notification)) {
    gpr_free(rc);
    error = GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
    goto done;
  }
  grpc_cq_begin_op(cq_for_notification, tag);
  rc->type = REGISTERED_CALL;
  rc->server = server;
  rc->tag = tag;
  rc->cq_bound_to_call = cq_bound_to_call;
  rc->cq_for_notification = cq_for_notification;
  rc->call = call;
  rc->data.registered.registered_method = rm;
  rc->data.registered.deadline = deadline;
  rc->initial_metadata = initial_metadata;
  rc->data.registered.optional_payload = optional_payload;
  error = queue_call_request(&exec_ctx, server, rc);
done:
  grpc_exec_ctx_finish(&exec_ctx);
  return error;
}

static void publish_registered_or_batch(grpc_exec_ctx *exec_ctx,
                                        void *user_data, int success);

static void cpstr(char **dest, size_t *capacity, grpc_mdstr *value) {
  gpr_slice slice = value->slice;
  size_t len = GPR_SLICE_LENGTH(slice);

  if (len + 1 > *capacity) {
    *capacity = GPR_MAX(len + 1, *capacity * 2);
    *dest = gpr_realloc(*dest, *capacity);
  }
  memcpy(*dest, grpc_mdstr_as_c_string(value), len + 1);
}

static void begin_call(grpc_exec_ctx *exec_ctx, grpc_server *server,
                       call_data *calld, requested_call *rc) {
  grpc_op ops[1];
  grpc_op *op = ops;

  memset(ops, 0, sizeof(ops));

  /* called once initial metadata has been read by the call, but BEFORE
     the ioreq to fetch it out of the call has been executed.
     This means metadata related fields can be relied on in calld, but to
     fill in the metadata array passed by the client, we need to perform
     an ioreq op, that should complete immediately. */

  grpc_call_set_completion_queue(exec_ctx, calld->call, rc->cq_bound_to_call);
  grpc_closure_init(&rc->publish, publish_registered_or_batch, rc);
  *rc->call = calld->call;
  calld->cq_new = rc->cq_for_notification;
  GPR_SWAP(grpc_metadata_array, *rc->initial_metadata, calld->initial_metadata);
  switch (rc->type) {
    case BATCH_CALL:
      GPR_ASSERT(calld->host != NULL);
      GPR_ASSERT(calld->path != NULL);
      cpstr(&rc->data.batch.details->host,
            &rc->data.batch.details->host_capacity, calld->host);
      cpstr(&rc->data.batch.details->method,
            &rc->data.batch.details->method_capacity, calld->path);
      rc->data.batch.details->deadline = calld->deadline;
      break;
    case REGISTERED_CALL:
      *rc->data.registered.deadline = calld->deadline;
      if (rc->data.registered.optional_payload) {
        op->op = GRPC_OP_RECV_MESSAGE;
        op->data.recv_message = rc->data.registered.optional_payload;
        op++;
      }
      break;
    default:
      GPR_UNREACHABLE_CODE(return );
  }

  GRPC_CALL_INTERNAL_REF(calld->call, "server");
  grpc_call_start_batch_and_execute(exec_ctx, calld->call, ops,
                                    (size_t)(op - ops), &rc->publish);
}

static void done_request_event(grpc_exec_ctx *exec_ctx, void *req,
                               grpc_cq_completion *c) {
  requested_call *rc = req;
  grpc_server *server = rc->server;

  if (rc >= server->requested_calls &&
      rc < server->requested_calls + server->max_requested_calls) {
    GPR_ASSERT(rc - server->requested_calls <= INT_MAX);
    gpr_stack_lockfree_push(server->request_freelist,
                            (int)(rc - server->requested_calls));
  } else {
    gpr_free(req);
  }

  server_unref(exec_ctx, server);
}

static void fail_call(grpc_exec_ctx *exec_ctx, grpc_server *server,
                      requested_call *rc) {
  *rc->call = NULL;
  rc->initial_metadata->count = 0;

  server_ref(server);
  grpc_cq_end_op(exec_ctx, rc->cq_for_notification, rc->tag, 0,
                 done_request_event, rc, &rc->completion);
}

static void publish_registered_or_batch(grpc_exec_ctx *exec_ctx, void *prc,
                                        int success) {
  requested_call *rc = prc;
  grpc_call *call = *rc->call;
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  server_ref(chand->server);
  grpc_cq_end_op(exec_ctx, calld->cq_new, rc->tag, success, done_request_event,
                 rc, &rc->completion);
  GRPC_CALL_INTERNAL_UNREF(exec_ctx, call, "server");
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
