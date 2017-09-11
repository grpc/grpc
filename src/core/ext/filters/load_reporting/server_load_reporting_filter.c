/*
 *
 * Copyright 2016 gRPC authors.
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

#include <string.h>

#include <grpc/load_reporting.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"
#include "src/core/ext/filters/load_reporting/server_load_reporting_plugin.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct call_data {
  intptr_t id; /**< an id unique to the call */
  bool have_trailing_md_string;
  grpc_slice trailing_md_string;
  bool have_initial_md_string;
  grpc_slice initial_md_string;
  bool have_service_method;
  grpc_slice service_method;

  /* stores the recv_initial_metadata op's ready closure, which we wrap with our
   * own (on_initial_md_ready) in order to capture the incoming initial metadata
   * */
  grpc_closure *ops_recv_initial_metadata_ready;

  /* to get notified of the availability of the incoming initial metadata. */
  grpc_closure on_initial_md_ready;
  grpc_metadata_batch *recv_initial_metadata;
} call_data;

typedef struct channel_data {
  intptr_t id; /**< an id unique to the channel */
} channel_data;

static void on_initial_md_ready(grpc_exec_ctx *exec_ctx, void *user_data,
                                grpc_error *err) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = (call_data *)elem->call_data;

  if (err == GRPC_ERROR_NONE) {
    if (calld->recv_initial_metadata->idx.named.path != NULL) {
      calld->service_method = grpc_slice_ref_internal(
          GRPC_MDVALUE(calld->recv_initial_metadata->idx.named.path->md));
      calld->have_service_method = true;
    } else {
      err = grpc_error_add_child(
          err, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing :path header"));
    }
    if (calld->recv_initial_metadata->idx.named.lb_token != NULL) {
      calld->initial_md_string = grpc_slice_ref_internal(
          GRPC_MDVALUE(calld->recv_initial_metadata->idx.named.lb_token->md));
      calld->have_initial_md_string = true;
      grpc_metadata_batch_remove(
          exec_ctx, calld->recv_initial_metadata,
          calld->recv_initial_metadata->idx.named.lb_token);
    }
  } else {
    GRPC_ERROR_REF(err);
  }
  calld->ops_recv_initial_metadata_ready->cb(
      exec_ctx, calld->ops_recv_initial_metadata_ready->cb_arg, err);
  GRPC_ERROR_UNREF(err);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = (call_data *)elem->call_data;
  calld->id = (intptr_t)args->call_stack;
  GRPC_CLOSURE_INIT(&calld->on_initial_md_ready, on_initial_md_ready, elem,
                    grpc_schedule_on_exec_ctx);

  /* TODO(dgq): do something with the data
  channel_data *chand = elem->channel_data;
  grpc_load_reporting_call_data lr_call_data = {GRPC_LR_POINT_CALL_CREATION,
                                                (intptr_t)chand->id,
                                                (intptr_t)calld->id,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL};
  */

  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  call_data *calld = (call_data *)elem->call_data;

  /* TODO(dgq): do something with the data
  channel_data *chand = elem->channel_data;
  grpc_load_reporting_call_data lr_call_data = {GRPC_LR_POINT_CALL_DESTRUCTION,
                                                (intptr_t)chand->id,
                                                (intptr_t)calld->id,
                                                final_info,
                                                calld->initial_md_string,
                                                calld->trailing_md_string,
                                                calld->service_method};
  */

  if (calld->have_initial_md_string) {
    grpc_slice_unref_internal(exec_ctx, calld->initial_md_string);
  }
  if (calld->have_trailing_md_string) {
    grpc_slice_unref_internal(exec_ctx, calld->trailing_md_string);
  }
  if (calld->have_service_method) {
    grpc_slice_unref_internal(exec_ctx, calld->service_method);
  }
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);

  channel_data *chand = (channel_data *)elem->channel_data;
  chand->id = (intptr_t)args->channel_stack;

  /* TODO(dgq): do something with the data
  grpc_load_reporting_call_data lr_call_data = {GRPC_LR_POINT_CHANNEL_CREATION,
                                                (intptr_t)chand,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL};
                                                */

  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* TODO(dgq): do something with the data
  channel_data *chand = elem->channel_data;
  grpc_load_reporting_call_data lr_call_data = {
      GRPC_LR_POINT_CHANNEL_DESTRUCTION,
      (intptr_t)chand->id,
      0,
      NULL,
      NULL,
      NULL,
      NULL};
  */
}

static grpc_filtered_mdelem lr_trailing_md_filter(grpc_exec_ctx *exec_ctx,
                                                  void *user_data,
                                                  grpc_mdelem md) {
  grpc_call_element *elem = (grpc_call_element *)user_data;
  call_data *calld = (call_data *)elem->call_data;
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_LB_COST_BIN)) {
    calld->trailing_md_string = GRPC_MDVALUE(md);
    return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

static void lr_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *op) {
  GPR_TIMER_BEGIN("lr_start_transport_stream_op_batch", 0);
  call_data *calld = (call_data *)elem->call_data;

  if (op->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->ops_recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->on_initial_md_ready;
  } else if (op->send_trailing_metadata) {
    GRPC_LOG_IF_ERROR(
        "grpc_metadata_batch_filter",
        grpc_metadata_batch_filter(
            exec_ctx,
            op->payload->send_trailing_metadata.send_trailing_metadata,
            lr_trailing_md_filter, elem,
            "LR trailing metadata filtering error"));
  }
  grpc_call_next_op(exec_ctx, elem, op);

  GPR_TIMER_END("lr_start_transport_stream_op_batch", 0);
}

const grpc_channel_filter grpc_server_load_reporting_filter = {
    lr_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "load_reporting"};
