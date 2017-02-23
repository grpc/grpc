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

#include "src/core/ext/census/grpc_filter.h"

#include <stdio.h>
#include <string.h>

#include <grpc/census.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/census/census_interface.h"
#include "src/core/ext/census/census_rpc_stats.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct call_data {
  census_op_id op_id;
  census_context *ctxt;
  gpr_timespec start_ts;
  int error;

  /* recv callback */
  grpc_metadata_batch *recv_initial_metadata;
  grpc_closure *on_done_recv;
  grpc_closure finish_recv;
} call_data;

typedef struct channel_data { uint8_t unused; } channel_data;

static void extract_and_annotate_method_tag(grpc_metadata_batch *md,
                                            call_data *calld,
                                            channel_data *chand) {
  grpc_linked_mdelem *m;
  for (m = md->list.head; m != NULL; m = m->next) {
    if (grpc_slice_eq(GRPC_MDKEY(m->md), GRPC_MDSTR_PATH)) {
      /* Add method tag here */
    }
  }
}

static void client_mutate_op(grpc_call_element *elem,
                             grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (op->send_initial_metadata) {
    extract_and_annotate_method_tag(op->send_initial_metadata, calld, chand);
  }
}

static void client_start_transport_op(grpc_exec_ctx *exec_ctx,
                                      grpc_call_element *elem,
                                      grpc_transport_stream_op *op) {
  client_mutate_op(elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
}

static void server_on_done_recv(grpc_exec_ctx *exec_ctx, void *ptr,
                                grpc_error *error) {
  GPR_TIMER_BEGIN("census-server:server_on_done_recv", 0);
  grpc_call_element *elem = ptr;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (error == GRPC_ERROR_NONE) {
    extract_and_annotate_method_tag(calld->recv_initial_metadata, calld, chand);
  }
  calld->on_done_recv->cb(exec_ctx, calld->on_done_recv->cb_arg, error);
  GPR_TIMER_END("census-server:server_on_done_recv", 0);
}

static void server_mutate_op(grpc_call_element *elem,
                             grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  if (op->recv_initial_metadata) {
    /* substitute our callback for the op callback */
    calld->recv_initial_metadata = op->recv_initial_metadata;
    calld->on_done_recv = op->recv_initial_metadata_ready;
    op->recv_initial_metadata_ready = &calld->finish_recv;
  }
}

static void server_start_transport_op(grpc_exec_ctx *exec_ctx,
                                      grpc_call_element *elem,
                                      grpc_transport_stream_op *op) {
  /* TODO(ctiller): this code fails. I don't know why. I expect it's
                    incomplete, and someone should look at it soon.

  call_data *calld = elem->call_data;
  GPR_ASSERT((calld->op_id.upper != 0) || (calld->op_id.lower != 0)); */
  server_mutate_op(elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
}

static grpc_error *client_init_call_elem(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         const grpc_call_element_args *args) {
  call_data *d = elem->call_data;
  GPR_ASSERT(d != NULL);
  memset(d, 0, sizeof(*d));
  d->start_ts = args->start_time;
  return GRPC_ERROR_NONE;
}

static void client_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_call_element *elem,
                                     const grpc_call_final_info *final_info,
                                     void *ignored) {
  call_data *d = elem->call_data;
  GPR_ASSERT(d != NULL);
  /* TODO(hongyu): record rpc client stats and census_rpc_end_op here */
}

static grpc_error *server_init_call_elem(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         const grpc_call_element_args *args) {
  call_data *d = elem->call_data;
  GPR_ASSERT(d != NULL);
  memset(d, 0, sizeof(*d));
  d->start_ts = args->start_time;
  /* TODO(hongyu): call census_tracing_start_op here. */
  grpc_closure_init(&d->finish_recv, server_on_done_recv, elem,
                    grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

static void server_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_call_element *elem,
                                     const grpc_call_final_info *final_info,
                                     void *ignored) {
  call_data *d = elem->call_data;
  GPR_ASSERT(d != NULL);
  /* TODO(hongyu): record rpc server stats and census_tracing_end_op here */
}

static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(chand != NULL);
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(chand != NULL);
}

const grpc_channel_filter grpc_client_census_filter = {
    client_start_transport_op,
    grpc_channel_next_op,
    sizeof(call_data),
    client_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    client_destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "census-client"};

const grpc_channel_filter grpc_server_census_filter = {
    server_start_transport_op,
    grpc_channel_next_op,
    sizeof(call_data),
    server_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    server_destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "census-server"};
