/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"

#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/profiling/timers.h"

static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void destroy_channel_elem(grpc_channel_element* elem) {}

namespace {
struct call_data {
  // Stats object to update.
  grpc_grpclb_client_stats* client_stats;
  // State for intercepting send_initial_metadata.
  grpc_closure on_complete_for_send;
  grpc_closure* original_on_complete_for_send;
  bool send_initial_metadata_succeeded;
  // State for intercepting recv_initial_metadata.
  grpc_closure recv_initial_metadata_ready;
  grpc_closure* original_recv_initial_metadata_ready;
  bool recv_initial_metadata_succeeded;
};
}  // namespace

static void on_complete_for_send(void* arg, grpc_error* error) {
  call_data* calld = (call_data*)arg;
  if (error == GRPC_ERROR_NONE) {
    calld->send_initial_metadata_succeeded = true;
  }
  GRPC_CLOSURE_RUN(calld->original_on_complete_for_send, GRPC_ERROR_REF(error));
}

static void recv_initial_metadata_ready(void* arg, grpc_error* error) {
  call_data* calld = (call_data*)arg;
  if (error == GRPC_ERROR_NONE) {
    calld->recv_initial_metadata_succeeded = true;
  }
  GRPC_CLOSURE_RUN(calld->original_recv_initial_metadata_ready,
                   GRPC_ERROR_REF(error));
}

static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  call_data* calld = (call_data*)elem->call_data;
  // Get stats object from context and take a ref.
  GPR_ASSERT(args->context != nullptr);
  GPR_ASSERT(args->context[GRPC_GRPCLB_CLIENT_STATS].value != nullptr);
  calld->client_stats = grpc_grpclb_client_stats_ref(
      (grpc_grpclb_client_stats*)args->context[GRPC_GRPCLB_CLIENT_STATS].value);
  // Record call started.
  grpc_grpclb_client_stats_add_call_started(calld->client_stats);
  return GRPC_ERROR_NONE;
}

static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  call_data* calld = (call_data*)elem->call_data;
  // Record call finished, optionally setting client_failed_to_send and
  // received.
  grpc_grpclb_client_stats_add_call_finished(
      !calld->send_initial_metadata_succeeded /* client_failed_to_send */,
      calld->recv_initial_metadata_succeeded /* known_received */,
      calld->client_stats);
  // All done, so unref the stats object.
  grpc_grpclb_client_stats_unref(calld->client_stats);
}

static void start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = (call_data*)elem->call_data;
  GPR_TIMER_BEGIN("clr_start_transport_stream_op_batch", 0);
  // Intercept send_initial_metadata.
  if (batch->send_initial_metadata) {
    calld->original_on_complete_for_send = batch->on_complete;
    GRPC_CLOSURE_INIT(&calld->on_complete_for_send, on_complete_for_send, calld,
                      grpc_schedule_on_exec_ctx);
    batch->on_complete = &calld->on_complete_for_send;
  }
  // Intercept recv_initial_metadata.
  if (batch->recv_initial_metadata) {
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    GRPC_CLOSURE_INIT(&calld->recv_initial_metadata_ready,
                      recv_initial_metadata_ready, calld,
                      grpc_schedule_on_exec_ctx);
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }
  // Chain to next filter.
  grpc_call_next_op(elem, batch);
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
    grpc_channel_next_get_info,
    "client_load_reporting"};
