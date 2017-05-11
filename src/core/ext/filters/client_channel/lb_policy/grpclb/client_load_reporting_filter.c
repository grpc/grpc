/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"

#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/profiling/timers.h"

static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {}

typedef struct {
  // Stats object to update.
  grpc_grpclb_client_stats *client_stats;
  // State for intercepting send_initial_metadata.
  grpc_closure on_complete_for_send;
  grpc_closure *original_on_complete_for_send;
  bool send_initial_metadata_succeeded;
  // State for intercepting recv_initial_metadata.
  grpc_closure recv_initial_metadata_ready;
  grpc_closure *original_recv_initial_metadata_ready;
  bool recv_initial_metadata_succeeded;
} call_data;

static void on_complete_for_send(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  call_data *calld = arg;
  if (error == GRPC_ERROR_NONE) {
    calld->send_initial_metadata_succeeded = true;
  }
  grpc_closure_run(exec_ctx, calld->original_on_complete_for_send,
                   GRPC_ERROR_REF(error));
}

static void recv_initial_metadata_ready(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  call_data *calld = arg;
  if (error == GRPC_ERROR_NONE) {
    calld->recv_initial_metadata_succeeded = true;
  }
  grpc_closure_run(exec_ctx, calld->original_recv_initial_metadata_ready,
                   GRPC_ERROR_REF(error));
}

static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  // Get stats object from context and take a ref.
  GPR_ASSERT(args->context != NULL);
  GPR_ASSERT(args->context[GRPC_GRPCLB_CLIENT_STATS].value != NULL);
  calld->client_stats = grpc_grpclb_client_stats_ref(
      args->context[GRPC_GRPCLB_CLIENT_STATS].value);
  // Record call started.
  grpc_grpclb_client_stats_add_call_started(calld->client_stats);
  return GRPC_ERROR_NONE;
}

static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  call_data *calld = elem->call_data;
  // Record call finished, optionally setting client_failed_to_send and
  // received.
  grpc_grpclb_client_stats_add_call_finished(
      false /* drop_for_rate_limiting */, false /* drop_for_load_balancing */,
      !calld->send_initial_metadata_succeeded /* client_failed_to_send */,
      calld->recv_initial_metadata_succeeded /* known_received */,
      calld->client_stats);
  // All done, so unref the stats object.
  grpc_grpclb_client_stats_unref(calld->client_stats);
}

static void start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  GPR_TIMER_BEGIN("clr_start_transport_stream_op_batch", 0);
  // Intercept send_initial_metadata.
  if (batch->send_initial_metadata) {
    calld->original_on_complete_for_send = batch->on_complete;
    grpc_closure_init(&calld->on_complete_for_send, on_complete_for_send, calld,
                      grpc_schedule_on_exec_ctx);
    batch->on_complete = &calld->on_complete_for_send;
  }
  // Intercept recv_initial_metadata.
  if (batch->recv_initial_metadata) {
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    grpc_closure_init(&calld->recv_initial_metadata_ready,
                      recv_initial_metadata_ready, calld,
                      grpc_schedule_on_exec_ctx);
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }
  // Chain to next filter.
  grpc_call_next_op(exec_ctx, elem, batch);
  GPR_TIMER_END("clr_start_transport_stream_op_batch", 0);
}

const grpc_channel_filter grpc_client_load_reporting_filter = {
    start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,  // sizeof(channel_data)
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "client_load_reporting"};
