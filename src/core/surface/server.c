/*
 *
 * Copyright 2014, Google Inc.
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
#include "src/core/transport/metadata.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

typedef enum { PENDING_START, ALL_CALLS, CALL_LIST_COUNT } call_list;

typedef struct listener {
  void *arg;
  void (*start)(grpc_server *server, void *arg, grpc_pollset *pollset);
  void (*destroy)(grpc_server *server, void *arg);
  struct listener *next;
} listener;

typedef struct call_data call_data;
typedef struct channel_data channel_data;

struct channel_data {
  grpc_server *server;
  grpc_channel *channel;
  grpc_mdstr *path_key;
  grpc_mdstr *authority_key;
  /* linked list of all channels on a server */
  channel_data *next;
  channel_data *prev;
};

typedef void (*new_call_cb)(grpc_server *server, grpc_completion_queue *cq,
                            grpc_metadata_array *initial_metadata,
                            call_data *calld, void *user_data);

typedef struct {
  void *user_data;
  grpc_completion_queue *cq;
  grpc_metadata_array *initial_metadata;
  new_call_cb cb;
} requested_call;

struct grpc_server {
  size_t channel_filter_count;
  const grpc_channel_filter **channel_filters;
  grpc_channel_args *channel_args;
  grpc_completion_queue *cq;

  gpr_mu mu;

  requested_call *requested_calls;
  size_t requested_call_count;
  size_t requested_call_capacity;

  gpr_uint8 shutdown;
  gpr_uint8 have_shutdown_tag;
  void *shutdown_tag;

  call_data *lists[CALL_LIST_COUNT];
  channel_data root_channel_data;

  listener *listeners;
  gpr_refcount internal_refcount;
};

typedef struct {
  call_data *next;
  call_data *prev;
} call_link;

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

typedef struct legacy_data { grpc_metadata_array *initial_metadata; } legacy_data;

struct call_data {
  grpc_call *call;

  call_state state;
  gpr_timespec deadline;
  grpc_mdstr *path;
  grpc_mdstr *host;

  legacy_data *legacy;

  gpr_uint8 included[CALL_LIST_COUNT];
  call_link links[CALL_LIST_COUNT];
};

#define SERVER_FROM_CALL_ELEM(elem) \
  (((channel_data *)(elem)->channel_data)->server)

static void do_nothing(void *unused, grpc_op_error ignored) {}

static int call_list_join(grpc_server *server, call_data *call,
                          call_list list) {
  if (call->included[list]) return 0;
  call->included[list] = 1;
  if (!server->lists[list]) {
    server->lists[list] = call;
    call->links[list].next = call->links[list].prev = call;
  } else {
    call->links[list].next = server->lists[list];
    call->links[list].prev = server->lists[list]->links[list].prev;
    call->links[list].next->links[list].prev =
        call->links[list].prev->links[list].next = call;
  }
  return 1;
}

static call_data *call_list_remove_head(grpc_server *server, call_list list) {
  call_data *out = server->lists[list];
  if (out) {
    out->included[list] = 0;
    if (out->links[list].next == out) {
      server->lists[list] = NULL;
    } else {
      server->lists[list] = out->links[list].next;
      out->links[list].next->links[list].prev = out->links[list].prev;
      out->links[list].prev->links[list].next = out->links[list].next;
    }
  }
  return out;
}

static int call_list_remove(grpc_server *server, call_data *call,
                            call_list list) {
  if (!call->included[list]) return 0;
  call->included[list] = 0;
  if (server->lists[list] == call) {
    server->lists[list] = call->links[list].next;
    if (server->lists[list] == call) {
      server->lists[list] = NULL;
      return 1;
    }
  }
  GPR_ASSERT(server->lists[list] != call);
  call->links[list].next->links[list].prev = call->links[list].prev;
  call->links[list].prev->links[list].next = call->links[list].next;
  return 1;
}

static void server_ref(grpc_server *server) {
  gpr_ref(&server->internal_refcount);
}

static void server_unref(grpc_server *server) {
  if (gpr_unref(&server->internal_refcount)) {
    grpc_channel_args_destroy(server->channel_args);
    gpr_mu_destroy(&server->mu);
    gpr_free(server->channel_filters);
    gpr_free(server->requested_calls);
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
  /*gpr_log(GPR_INFO, "destroy channel %p", chand->channel);*/
  grpc_channel_destroy(chand->channel);
  server_unref(server);
}

static void destroy_channel(channel_data *chand) {
  if (is_channel_orphaned(chand)) return;
  GPR_ASSERT(chand->server != NULL);
  orphan_channel(chand);
  server_ref(chand->server);
  grpc_iomgr_add_callback(finish_destroy_channel, chand);
}

static void start_new_rpc(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  grpc_server *server = chand->server;

  gpr_mu_lock(&server->mu);
  if (server->requested_call_count > 0) {
    requested_call rc = server->requested_calls[--server->requested_call_count];
    calld->state = ACTIVATED;
    gpr_mu_unlock(&server->mu);
    rc.cb(server, rc.cq, rc.initial_metadata, calld, rc.user_data);
  } else {
    calld->state = PENDING;
    call_list_join(server, calld, PENDING_START);
    gpr_mu_unlock(&server->mu);
  }
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
      grpc_call_stream_closed(elem);
      break;
    case PENDING:
      call_list_remove(chand->server, calld, PENDING_START);
    /* fallthrough intended */
    case NOT_STARTED:
      calld->state = ZOMBIED;
      grpc_iomgr_add_callback(kill_zombie, elem);
      break;
    case ZOMBIED:
      break;
  }
  gpr_mu_unlock(&chand->server->mu);
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

  switch (op->type) {
    case GRPC_ACCEPT_CALL:
      /* create a call */
      grpc_call_create(chand->channel,
                       op->data.accept_call.transport_server_data);
      break;
    case GRPC_TRANSPORT_CLOSED:
      /* if the transport is closed for a server channel, we destroy the
         channel */
      gpr_mu_lock(&chand->server->mu);
      server_ref(chand->server);
      destroy_channel(chand);
      gpr_mu_unlock(&chand->server->mu);
      server_unref(chand->server);
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
  call_list_join(chand->server, calld, ALL_CALLS);
  gpr_mu_unlock(&chand->server->mu);

  server_ref(chand->server);
}

static void destroy_call_elem(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  int i;

  gpr_mu_lock(&chand->server->mu);
  for (i = 0; i < CALL_LIST_COUNT; i++) {
    call_list_remove(chand->server, elem->call_data, i);
  }
  if (chand->server->shutdown && chand->server->have_shutdown_tag &&
      chand->server->lists[ALL_CALLS] == NULL) {
    grpc_cq_end_server_shutdown(chand->server->cq, chand->server->shutdown_tag);
  }
  gpr_mu_unlock(&chand->server->mu);

  if (calld->host) {
    grpc_mdstr_unref(calld->host);
  }
  if (calld->path) {
    grpc_mdstr_unref(calld->path);
  }

  if (calld->legacy) {
    gpr_free(calld->legacy->initial_metadata->metadata);
    gpr_free(calld->legacy->initial_metadata);
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
}

static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
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

grpc_server *grpc_server_create_from_filters(grpc_completion_queue *cq,
                                             grpc_channel_filter **filters,
                                             size_t filter_count,
                                             const grpc_channel_args *args) {
  size_t i;
  int census_enabled = grpc_channel_args_is_census_enabled(args);

  grpc_server *server = gpr_malloc(sizeof(grpc_server));
  memset(server, 0, sizeof(grpc_server));

  gpr_mu_init(&server->mu);

  server->cq = cq;
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

void grpc_server_start(grpc_server *server) {
  listener *l;

  for (l = server->listeners; l; l = l->next) {
    l->start(server, l->arg, grpc_cq_pollset(server->cq));
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
  grpc_channel *channel;
  channel_data *chand;

  for (i = 0; i < s->channel_filter_count; i++) {
    filters[i] = s->channel_filters[i];
  }
  for (; i < s->channel_filter_count + num_extra_filters; i++) {
    filters[i] = extra_filters[i - s->channel_filter_count];
  }
  filters[i] = &grpc_connected_channel_filter;

  grpc_transport_add_to_pollset(transport, grpc_cq_pollset(s->cq));

  channel = grpc_channel_create_from_filters(filters, num_filters,
                                             s->channel_args, mdctx, 0);
  chand = (channel_data *)grpc_channel_stack_element(
              grpc_channel_get_channel_stack(channel), 0)->channel_data;
  chand->server = s;
  server_ref(s);
  chand->channel = channel;

  gpr_mu_lock(&s->mu);
  chand->next = &s->root_channel_data;
  chand->prev = chand->next->prev;
  chand->next->prev = chand->prev->next = chand;
  gpr_mu_unlock(&s->mu);

  gpr_free(filters);

  return grpc_connected_channel_bind_transport(
      grpc_channel_get_channel_stack(channel), transport);
}

void shutdown_internal(grpc_server *server, gpr_uint8 have_shutdown_tag,
                       void *shutdown_tag) {
  listener *l;
  requested_call *requested_calls;
  size_t requested_call_count;
  channel_data **channels;
  channel_data *c;
  size_t nchannels;
  size_t i;
  grpc_channel_op op;
  grpc_channel_element *elem;

  /* lock, and gather up some stuff to do */
  gpr_mu_lock(&server->mu);
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

  requested_calls = server->requested_calls;
  requested_call_count = server->requested_call_count;
  server->requested_calls = NULL;
  server->requested_call_count = 0;

  server->shutdown = 1;
  server->have_shutdown_tag = have_shutdown_tag;
  server->shutdown_tag = shutdown_tag;
  if (have_shutdown_tag) {
    grpc_cq_begin_op(server->cq, NULL, GRPC_SERVER_SHUTDOWN);
    if (server->lists[ALL_CALLS] == NULL) {
      grpc_cq_end_server_shutdown(server->cq, shutdown_tag);
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
  for (i = 0; i < requested_call_count; i++) {
    requested_calls[i].cb(server, requested_calls[i].cq,
                          requested_calls[i].initial_metadata, NULL,
                          requested_calls[i].user_data);
  }
  gpr_free(requested_calls);

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
  for (c = server->root_channel_data.next; c != &server->root_channel_data;
       c = c->next) {
    shutdown_channel(c);
  }
  gpr_mu_unlock(&server->mu);

  server_unref(server);
}

void grpc_server_add_listener(grpc_server *server, void *arg,
                              void (*start)(grpc_server *server, void *arg,
                                            grpc_pollset *pollset),
                              void (*destroy)(grpc_server *server, void *arg)) {
  listener *l = gpr_malloc(sizeof(listener));
  l->arg = arg;
  l->start = start;
  l->destroy = destroy;
  l->next = server->listeners;
  server->listeners = l;
}

static grpc_call_error queue_call_request(grpc_server *server,
                                          grpc_completion_queue *cq,
                                          grpc_metadata_array *initial_metadata,
                                          new_call_cb cb, void *user_data) {
  call_data *calld;
  requested_call *rc;
  gpr_mu_lock(&server->mu);
  if (server->shutdown) {
    gpr_mu_unlock(&server->mu);
    cb(server, cq, initial_metadata, NULL, user_data);
    return GRPC_CALL_OK;
  }
  calld = call_list_remove_head(server, PENDING_START);
  if (calld) {
    GPR_ASSERT(calld->state == PENDING);
    calld->state = ACTIVATED;
    gpr_mu_unlock(&server->mu);
    cb(server, cq, initial_metadata, calld, user_data);
    return GRPC_CALL_OK;
  } else {
    if (server->requested_call_count == server->requested_call_capacity) {
      server->requested_call_capacity =
          GPR_MAX(server->requested_call_capacity + 8,
                  server->requested_call_capacity * 2);
      server->requested_calls =
          gpr_realloc(server->requested_calls,
                      sizeof(requested_call) * server->requested_call_capacity);
    }
    rc = &server->requested_calls[server->requested_call_count++];
    rc->cb = cb;
    rc->cq = cq;
    rc->user_data = user_data;
    rc->initial_metadata = initial_metadata;
    gpr_mu_unlock(&server->mu);
    return GRPC_CALL_OK;
  }
}

static void begin_request(grpc_server *server, grpc_completion_queue *cq,
                          grpc_metadata_array *initial_metadata,
                          call_data *call_data, void *tag) {
  abort();
}

grpc_call_error grpc_server_request_call_old(
    grpc_server *server, grpc_call_details *details,
    grpc_metadata_array *initial_metadata, grpc_completion_queue *cq,
    void *tag) {
  grpc_cq_begin_op(cq, NULL, GRPC_IOREQ);
  return queue_call_request(server, cq, initial_metadata, begin_request, tag);
}

static void publish_legacy_request(grpc_call *call, grpc_op_error status,
                                   void *tag) {
  grpc_call_element *elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_server *server = chand->server;

  if (status == GRPC_OP_OK) {
    grpc_cq_end_new_rpc(server->cq, tag, call, do_nothing, NULL,
                        grpc_mdstr_as_c_string(calld->path),
                        grpc_mdstr_as_c_string(calld->host), calld->deadline,
                        calld->legacy->initial_metadata->count,
                        calld->legacy->initial_metadata->metadata);
  } else {
    abort();
  }
}

static void begin_legacy_request(grpc_server *server, grpc_completion_queue *cq,
                                 grpc_metadata_array *initial_metadata,
                                 call_data *calld, void *tag) {
  grpc_ioreq req;
  if (!calld) {
    gpr_free(initial_metadata);
    grpc_cq_end_new_rpc(cq, tag, NULL, do_nothing, NULL, NULL, NULL,
                        gpr_inf_past, 0, NULL);
    return;
  }
  req.op = GRPC_IOREQ_RECV_INITIAL_METADATA;
  req.data.recv_metadata = initial_metadata;
  calld->legacy = gpr_malloc(sizeof(legacy_data));
  memset(calld->legacy, 0, sizeof(legacy_data));
  calld->legacy->initial_metadata = initial_metadata;
  grpc_call_internal_ref(calld->call);
  grpc_call_start_ioreq_and_call_back(calld->call, &req, 1,
                                      publish_legacy_request, tag);
}

grpc_call_error grpc_server_request_call_old(grpc_server *server,
                                             void *tag_new) {
  grpc_metadata_array *client_metadata =
      gpr_malloc(sizeof(grpc_metadata_array));
  memset(client_metadata, 0, sizeof(*client_metadata));
  grpc_cq_begin_op(server->cq, NULL, GRPC_SERVER_RPC_NEW);
  return queue_call_request(server, server->cq, client_metadata,
                            begin_legacy_request, tag_new);
}

const grpc_channel_args *grpc_server_get_channel_args(grpc_server *server) {
  return server->channel_args;
}
