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

#include "src/core/client_config/subchannel.h"

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/channel/channel_args.h"

typedef struct {
  gpr_refcount refs;
  grpc_subchannel *subchannel;
} connection;

struct grpc_subchannel {
  gpr_refcount refs;
  grpc_connector *connector;

  /** non-transport related channel filters */
  const grpc_channel_filter **filters;
  size_t filter_count;
  /** channel arguments */
  grpc_channel_args *args;
  /** address to connect to */
  struct sockaddr *addr;
  size_t addr_len;

  /** set during connection */
  grpc_transport *connecting_transport;

  /** callback for connection finishing */
  grpc_iomgr_closure connected;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** active connection */
  connection *active;
  /** are we connecting */
  int connecting;
  /** closures waiting for a connection */
  grpc_iomgr_closure *waiting;
};

struct grpc_subchannel_call {
  connection *connection;
  gpr_refcount refs;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) (((grpc_call_stack *)(call)) + 1)

static grpc_subchannel_call *create_call(connection *con, grpc_transport_stream_op *initial_op);

/*
 * grpc_subchannel implementation
 */

void grpc_subchannel_ref(grpc_subchannel *c) { gpr_ref(&c->refs); }

void grpc_subchannel_unref(grpc_subchannel *c) {
  if (gpr_unref(&c->refs)) {
    gpr_free(c->filters);
    grpc_channel_args_destroy(c->args);
    gpr_free(c->addr);
    gpr_free(c);
  }
}

grpc_subchannel *grpc_subchannel_create(grpc_connector *connector,
                                        grpc_subchannel_args *args) {
  grpc_subchannel *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->refs, 1);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->filters = gpr_malloc(sizeof(grpc_channel_filter *) * args->filter_count);
  memcpy(c->filters, args->filters,
         sizeof(grpc_channel_filter *) * args->filter_count);
  c->filter_count = args->filter_count;
  c->addr = gpr_malloc(args->addr_len);
  memcpy(c->addr, args->addr, args->addr_len);
  c->addr_len = args->addr_len;
  c->args = grpc_channel_args_copy(args->args);
  gpr_mu_init(&c->mu);
  return c;
}

void grpc_subchannel_create_call(grpc_subchannel *c,
                                 grpc_mdctx *mdctx,
                                 grpc_transport_stream_op *initial_op,
                                 grpc_subchannel_call **target,
                                 grpc_iomgr_closure *notify) {
  connection *con;
  gpr_mu_lock(&c->mu);
  if (c->active != NULL) {
    con = c->active;
    gpr_ref(&con->refs);
    gpr_mu_unlock(&c->mu);

    *target = create_call(con, initial_op);
    notify->cb(notify->cb_arg, 1);
  } else {
    notify->next = c->waiting;
    c->waiting = notify;
    if (!c->connecting) {
      c->connecting = 1;
      gpr_mu_unlock(&c->mu);

      grpc_connector_connect(c->connector, c->args, mdctx, &c->connecting_transport, &c->connected);
    } else {
      gpr_mu_unlock(&c->mu);
    }
  }
}

/*
 * grpc_subchannel_call implementation
 */

void grpc_subchannel_call_ref(grpc_subchannel_call *call) {
  gpr_ref(&call->refs);
}

void grpc_subchannel_call_unref(grpc_subchannel_call *call) {
  if (gpr_unref(&call->refs)) {
    grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(call));
    if (gpr_unref(&call->connection->refs)) {
      gpr_free(call->connection);
    }
    gpr_free(call);
  }
}

void grpc_subchannel_call_process_op(grpc_subchannel_call *call,
                                     grpc_transport_stream_op *op) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(top_elem, op);
}

grpc_subchannel_call *create_call(connection *con, grpc_transport_stream_op *initial_op) {
  abort();
  return NULL;
}
