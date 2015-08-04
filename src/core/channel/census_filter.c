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

#include "src/core/channel/census_filter.h"

#include <stdio.h>
#include <string.h>

#include "src/core/channel/channel_stack.h"
#include "src/core/channel/noop_filter.h"
#include "src/core/statistics/census_interface.h"
#include "src/core/statistics/census_rpc_stats.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/time.h>

typedef struct call_data {
  census_op_id op_id;
  census_rpc_stats stats;
  gpr_timespec start_ts;

  /* recv callback */
  grpc_stream_op_buffer* recv_ops;
  void (*on_done_recv)(void* user_data, int success);
  void* recv_user_data;
} call_data;

typedef struct channel_data {
  grpc_mdstr* path_str; /* pointer to meta data str with key == ":path" */
} channel_data;

static void init_rpc_stats(census_rpc_stats* stats) {
  memset(stats, 0, sizeof(census_rpc_stats));
  stats->cnt = 1;
}

static void extract_and_annotate_method_tag(grpc_stream_op_buffer* sopb,
                                            call_data* calld,
                                            channel_data* chand) {
  grpc_linked_mdelem* m;
  size_t i;
  for (i = 0; i < sopb->nops; i++) {
    grpc_stream_op* op = &sopb->ops[i];
    if (op->type != GRPC_OP_METADATA) continue;
    for (m = op->data.metadata.list.head; m != NULL; m = m->next) {
      if (m->md->key == chand->path_str) {
        gpr_log(GPR_DEBUG, "%s",
                (const char*)GPR_SLICE_START_PTR(m->md->value->slice));
        census_add_method_tag(calld->op_id, (const char*)GPR_SLICE_START_PTR(
                                                m->md->value->slice));
      }
    }
  }
}

static void client_mutate_op(grpc_call_element* elem,
                             grpc_transport_stream_op* op) {
  call_data* calld = elem->call_data;
  channel_data* chand = elem->channel_data;
  if (op->send_ops) {
    extract_and_annotate_method_tag(op->send_ops, calld, chand);
  }
}

static void client_start_transport_op(grpc_call_element* elem,
                                      grpc_transport_stream_op* op) {
  call_data* calld = elem->call_data;
  GPR_ASSERT((calld->op_id.upper != 0) || (calld->op_id.lower != 0));
  client_mutate_op(elem, op);
  grpc_call_next_op(elem, op);
}

static void server_on_done_recv(void* ptr, int success) {
  grpc_call_element* elem = ptr;
  call_data* calld = elem->call_data;
  channel_data* chand = elem->channel_data;
  if (success) {
    extract_and_annotate_method_tag(calld->recv_ops, calld, chand);
  }
  calld->on_done_recv(calld->recv_user_data, success);
}

static void server_mutate_op(grpc_call_element* elem,
                             grpc_transport_stream_op* op) {
  call_data* calld = elem->call_data;
  if (op->recv_ops) {
    /* substitute our callback for the op callback */
    calld->recv_ops = op->recv_ops;
    calld->on_done_recv = op->on_done_recv;
    calld->recv_user_data = op->recv_user_data;
    op->on_done_recv = server_on_done_recv;
    op->recv_user_data = elem;
  }
}

static void server_start_transport_op(grpc_call_element* elem,
                                      grpc_transport_stream_op* op) {
  call_data* calld = elem->call_data;
  GPR_ASSERT((calld->op_id.upper != 0) || (calld->op_id.lower != 0));
  server_mutate_op(elem, op);
  grpc_call_next_op(elem, op);
}

static void channel_op(grpc_channel_element* elem,
                       grpc_channel_element* from_elem, grpc_channel_op* op) {
  switch (op->type) {
    case GRPC_TRANSPORT_CLOSED:
      /* TODO(hongyu): Annotate trace information for all calls of the channel
       */
      break;
    default:
      break;
  }
  grpc_channel_next_op(elem, op);
}

static void client_init_call_elem(grpc_call_element* elem,
                                  const void* server_transport_data,
                                  grpc_transport_stream_op* initial_op) {
  call_data* d = elem->call_data;
  GPR_ASSERT(d != NULL);
  init_rpc_stats(&d->stats);
  d->start_ts = gpr_now(GPR_CLOCK_REALTIME);
  d->op_id = census_tracing_start_op();
  if (initial_op) client_mutate_op(elem, initial_op);
}

static void client_destroy_call_elem(grpc_call_element* elem) {
  call_data* d = elem->call_data;
  GPR_ASSERT(d != NULL);
  census_record_rpc_client_stats(d->op_id, &d->stats);
  census_tracing_end_op(d->op_id);
}

static void server_init_call_elem(grpc_call_element* elem,
                                  const void* server_transport_data,
                                  grpc_transport_stream_op* initial_op) {
  call_data* d = elem->call_data;
  GPR_ASSERT(d != NULL);
  init_rpc_stats(&d->stats);
  d->start_ts = gpr_now(GPR_CLOCK_REALTIME);
  d->op_id = census_tracing_start_op();
  if (initial_op) server_mutate_op(elem, initial_op);
}

static void server_destroy_call_elem(grpc_call_element* elem) {
  call_data* d = elem->call_data;
  GPR_ASSERT(d != NULL);
  d->stats.elapsed_time_ms = gpr_timespec_to_micros(
      gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), d->start_ts));
  census_record_rpc_server_stats(d->op_id, &d->stats);
  census_tracing_end_op(d->op_id);
}

static void init_channel_elem(grpc_channel_element* elem,
                              const grpc_channel_args* args, grpc_mdctx* mdctx,
                              int is_first, int is_last) {
  channel_data* chand = elem->channel_data;
  GPR_ASSERT(chand != NULL);
  GPR_ASSERT(!is_first);
  GPR_ASSERT(!is_last);
  chand->path_str = grpc_mdstr_from_string(mdctx, ":path");
}

static void destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = elem->channel_data;
  GPR_ASSERT(chand != NULL);
  if (chand->path_str != NULL) {
    GRPC_MDSTR_UNREF(chand->path_str);
  }
}

const grpc_channel_filter grpc_client_census_filter = {
    client_start_transport_op,
    channel_op,
    sizeof(call_data),
    client_init_call_elem,
    client_destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    "census-client"};

const grpc_channel_filter grpc_server_census_filter = {
    server_start_transport_op,
    channel_op,
    sizeof(call_data),
    server_init_call_elem,
    server_destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    "census-server"};
