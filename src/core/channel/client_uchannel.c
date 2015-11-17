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

#include "src/core/channel/client_uchannel.h"

#include <string.h>

#include "src/core/census/grpc_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/client_channel.h"
#include "src/core/channel/compress_filter.h"
#include "src/core/channel/subchannel_call_holder.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/connectivity_state.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

/** Microchannel (uchannel) implementation: a lightweight channel without any
 * load-balancing mechanisms meant for communication from within the core. */

typedef struct client_uchannel_channel_data {
  /** metadata context for this channel */
  grpc_mdctx *mdctx;

  /** master channel - the grpc_channel instance that ultimately owns
      this channel_data via its channel stack.
      We occasionally use this to bump the refcount on the master channel
      to keep ourselves alive through an asynchronous operation. */
  grpc_channel *master;

  /** connectivity state being tracked */
  grpc_connectivity_state_tracker state_tracker;

  /** the subchannel wrapped by the microchannel */
  grpc_connected_subchannel *connected_subchannel;

  /** the callback used to stay subscribed to subchannel connectivity
   * notifications */
  grpc_closure connectivity_cb;

  /** the current connectivity state of the wrapped subchannel */
  grpc_connectivity_state subchannel_connectivity;

  gpr_mu mu_state;
} channel_data;

typedef grpc_subchannel_call_holder call_data;

static void monitor_subchannel(grpc_exec_ctx *exec_ctx, void *arg,
                               int iomgr_success) {
  channel_data *chand = arg;
  grpc_connectivity_state_set(exec_ctx, &chand->state_tracker,
                              chand->subchannel_connectivity,
                              "uchannel_monitor_subchannel");
  grpc_connected_subchannel_notify_on_state_change(exec_ctx, chand->connected_subchannel,
                                         &chand->subchannel_connectivity,
                                         &chand->connectivity_cb);
}

static char *cuc_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  return grpc_subchannel_call_holder_get_peer(exec_ctx, elem->call_data,
                                              chand->master);
}

static void cuc_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                          grpc_call_element *elem,
                                          grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  grpc_subchannel_call_holder_perform_op(exec_ctx, elem->call_data, op);
}

static void cuc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                   grpc_channel_element *elem,
                                   grpc_transport_op *op) {
  channel_data *chand = elem->channel_data;

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, 1);

  GPR_ASSERT(op->set_accept_stream == NULL);
  GPR_ASSERT(op->bind_pollset == NULL);

  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = NULL;
    op->connectivity_state = NULL;
  }

  if (op->disconnect) {
    grpc_connectivity_state_set(exec_ctx, &chand->state_tracker,
                                GRPC_CHANNEL_FATAL_FAILURE, "disconnect");
  }
}

static int cuc_pick_subchannel(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_metadata_batch *initial_metadata,
                               grpc_connected_subchannel **connected_subchannel,
                               grpc_closure *on_ready) {
  channel_data *chand = arg;
  GPR_ASSERT(initial_metadata != NULL);
  *connected_subchannel = chand->connected_subchannel;
  return 1;
}

/* Constructor for call_data */
static void cuc_init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                               grpc_call_element_args *args) {
  grpc_subchannel_call_holder_init(elem->call_data, cuc_pick_subchannel,
                                   elem->channel_data);
}

/* Destructor for call_data */
static void cuc_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem) {
  grpc_subchannel_call_holder_destroy(exec_ctx, elem->call_data);
}

/* Constructor for channel_data */
static void cuc_init_channel_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  memset(chand, 0, sizeof(*chand));
  grpc_closure_init(&chand->connectivity_cb, monitor_subchannel, chand);
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_uchannel_filter);
  chand->mdctx = args->metadata_context;
  chand->master = args->master;
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_uchannel");
  gpr_mu_init(&chand->mu_state);
}

/* Destructor for channel_data */
static void cuc_destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  grpc_connected_subchannel_state_change_unsubscribe(exec_ctx, chand->connected_subchannel,
                                           &chand->connectivity_cb);
  grpc_connectivity_state_destroy(exec_ctx, &chand->state_tracker);
  gpr_mu_destroy(&chand->mu_state);
}

static void cuc_set_pollset(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            grpc_pollset *pollset) {
  call_data *calld = elem->call_data;
  calld->pollset = pollset;
}

const grpc_channel_filter grpc_client_uchannel_filter = {
    cuc_start_transport_stream_op, cuc_start_transport_op, sizeof(call_data),
    cuc_init_call_elem, cuc_set_pollset, cuc_destroy_call_elem,
    sizeof(channel_data), cuc_init_channel_elem, cuc_destroy_channel_elem,
    cuc_get_peer, "client-uchannel",
};

grpc_connectivity_state grpc_client_uchannel_check_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, int try_to_connect) {
  channel_data *chand = elem->channel_data;
  grpc_connectivity_state out;
  out = grpc_connectivity_state_check(&chand->state_tracker);
  gpr_mu_lock(&chand->mu_state);
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    grpc_connectivity_state_set(exec_ctx, &chand->state_tracker,
                                GRPC_CHANNEL_CONNECTING,
                                "uchannel_connecting_changed");
    chand->subchannel_connectivity = out;
    grpc_connected_subchannel_notify_on_state_change(exec_ctx, chand->connected_subchannel,
                                           &chand->subchannel_connectivity,
                                           &chand->connectivity_cb);
  }
  gpr_mu_unlock(&chand->mu_state);
  return out;
}

void grpc_client_uchannel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
    grpc_connectivity_state *state, grpc_closure *on_complete) {
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->mu_state);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &chand->state_tracker, state, on_complete);
  gpr_mu_unlock(&chand->mu_state);
}

grpc_pollset_set *grpc_client_uchannel_get_connecting_pollset_set(
    grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  grpc_channel_element *parent_elem;
  gpr_mu_lock(&chand->mu_state);
  parent_elem = grpc_channel_stack_last_element(grpc_channel_get_channel_stack(
      chand->master));
  gpr_mu_unlock(&chand->mu_state);
  return grpc_client_channel_get_connecting_pollset_set(parent_elem);
}

void grpc_client_uchannel_add_interested_party(grpc_exec_ctx *exec_ctx,
                                               grpc_channel_element *elem,
                                               grpc_pollset *pollset) {
  grpc_pollset_set *master_pollset_set =
      grpc_client_uchannel_get_connecting_pollset_set(elem);
  grpc_pollset_set_add_pollset(exec_ctx, master_pollset_set, pollset);
}

void grpc_client_uchannel_del_interested_party(grpc_exec_ctx *exec_ctx,
                                               grpc_channel_element *elem,
                                               grpc_pollset *pollset) {
  grpc_pollset_set *master_pollset_set =
      grpc_client_uchannel_get_connecting_pollset_set(elem);
  grpc_pollset_set_del_pollset(exec_ctx, master_pollset_set, pollset);
}

grpc_channel *grpc_client_uchannel_create(grpc_subchannel *subchannel,
                                          grpc_channel_args *args) {
  grpc_channel *channel = NULL;
#define MAX_FILTERS 3
  const grpc_channel_filter *filters[MAX_FILTERS];
  grpc_mdctx *mdctx = grpc_subchannel_get_mdctx(subchannel);
  grpc_channel *master = grpc_subchannel_get_master(subchannel);
  char *target = grpc_channel_get_target(master);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t n = 0;

  grpc_mdctx_ref(mdctx);
  if (grpc_channel_args_is_census_enabled(args)) {
    filters[n++] = &grpc_client_census_filter;
  }
  filters[n++] = &grpc_compress_filter;
  filters[n++] = &grpc_client_uchannel_filter;
  GPR_ASSERT(n <= MAX_FILTERS);

  channel = grpc_channel_create_from_filters(&exec_ctx, target, filters, n,
                                             args, mdctx, 1);

  gpr_free(target);
  return channel;
}

void grpc_client_uchannel_set_connected_subchannel(grpc_channel *uchannel,
                                         grpc_connected_subchannel *connected_subchannel) {
  grpc_channel_element *elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(uchannel));
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_client_uchannel_filter);
  gpr_mu_lock(&chand->mu_state);
  chand->connected_subchannel = connected_subchannel;
  gpr_mu_unlock(&chand->mu_state);
}
