//
// Copyright 2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "src/core/lib/channel/deadline_filter.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/timer.h"

// Used for both client and server filters.
typedef struct channel_data {
} channel_data;

// Call data used for both client and server filter.
typedef struct base_call_data {
  grpc_call_stack* call_stack;
  bool timer_pending;
  grpc_timer timer;
} base_call_data;

// Additional call data used only for the server filter.
typedef struct server_call_data {
  base_call_data base;  // Must be first.
  // The closure for receiving initial metadata.
  grpc_closure recv_initial_metadata_ready;
  // Received initial metadata batch.
  grpc_metadata_batch* recv_initial_metadata;
  // The original recv_initial_metadata_ready closure, which we chain to
  // after our own closure is invoked.
  grpc_closure* next_recv_initial_metadata_ready;
} server_call_data;

// Constructor for channel_data.  Used for both client and server filters.
static void init_channel_elem(grpc_exec_ctx* exec_ctx,
                              grpc_channel_element* elem,
                              grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
}

// Destructor for channel_data.  Used for both client and server filters.
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {
}

// Constructor for call_data.  Used for both client and server filters.
static grpc_error *init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  grpc_call_element_args* args) {
  base_call_data* calld = elem->call_data;
  // Note: size of call data is different between client and server.
  memset(calld, 0, elem->filter->sizeof_call_data);
  calld->call_stack = args->call_stack;
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.  Used for both client and server filters.
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              void* and_free_memory) {
  base_call_data* calld = elem->call_data;
gpr_log(GPR_INFO, "==> destroy_call_elem()");
// FIXME: this is not working -- timer holds a ref, so we won't get
// called until after timer pops
  if (calld->timer_pending)
{
gpr_log(GPR_INFO, "CANCELLING TIMER");
    grpc_timer_cancel(exec_ctx, &calld->timer);
}

}

// Timer callback.
static void timer_callback(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  grpc_call_element* elem = arg;
  base_call_data* calld = elem->call_data;
  calld->timer_pending = false;
  if (error != GRPC_ERROR_CANCELLED) {
gpr_log(GPR_INFO, "DEADLINE EXCEEDED");
    gpr_slice message = gpr_slice_from_static_string("Deadline Exceeded");
    grpc_call_element_send_cancel_with_message(
        exec_ctx, elem, GRPC_STATUS_DEADLINE_EXCEEDED, &message);
  }
else gpr_log(GPR_INFO, "TIMER CANCELLED");
gpr_log(GPR_INFO, "UNREF");
  GRPC_CALL_STACK_UNREF(exec_ctx, calld->call_stack, "deadline");
}

// Starts the deadline timer.
static void start_timer_if_needed(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element* elem,
                                  gpr_timespec deadline) {
  base_call_data* calld = elem->call_data;
  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);
  if (gpr_time_cmp(deadline, gpr_inf_future(GPR_CLOCK_MONOTONIC)) != 0) {
    // Take a reference to the call stack, to be owned by the timer.
gpr_log(GPR_INFO, "REF");
    GRPC_CALL_STACK_REF(calld->call_stack, "deadline");
    grpc_timer_init(exec_ctx, &calld->timer, deadline, timer_callback, elem,
                    gpr_now(GPR_CLOCK_MONOTONIC));
    calld->timer_pending = true;
  }
}

// Method for starting a call op for client filter.
static void client_start_transport_stream_op(grpc_exec_ctx* exec_ctx,
                                             grpc_call_element* elem,
                                             grpc_transport_stream_op* op) {
  // If we're sending initial metadata, get the deadline from the metadata
  // and start the timer if needed.
  if (op->send_initial_metadata != NULL) {
    start_timer_if_needed(exec_ctx, elem,
                          op->send_initial_metadata->deadline);
  }
  // Chain to next filter.
  grpc_call_next_op(exec_ctx, elem, op);
}

// Callback for receiving initial metadata on the server.
static void recv_initial_metadata_ready(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  grpc_call_element* elem = arg;
  server_call_data* calld = elem->call_data;
  // Get deadline from metadata and start the timer if needed.
  start_timer_if_needed(exec_ctx, elem,
                        calld->recv_initial_metadata->deadline);
  // Invoke the next callback.
  calld->next_recv_initial_metadata_ready->cb(
      exec_ctx, calld->next_recv_initial_metadata_ready->cb_arg, error);
}

// Method for starting a call op for server filter.
static void server_start_transport_stream_op(grpc_exec_ctx* exec_ctx,
                                             grpc_call_element* elem,
                                             grpc_transport_stream_op* op) {
  server_call_data* calld = elem->call_data;
  // If we're receiving initial metadata, we need to get the deadline
  // from the recv_initial_metadata_ready callback.  So we inject our
  // own callback into that hook.
  if (op->recv_initial_metadata_ready != NULL) {
    calld->next_recv_initial_metadata_ready = op->recv_initial_metadata_ready;
    calld->recv_initial_metadata = op->recv_initial_metadata;
    grpc_closure_init(&calld->recv_initial_metadata_ready,
                      recv_initial_metadata_ready, elem);
    op->recv_initial_metadata_ready = &calld->recv_initial_metadata_ready;
  }
  // Chain to next filter.
  grpc_call_next_op(exec_ctx, elem, op);
}

const grpc_channel_filter grpc_client_deadline_filter = {
    client_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(base_call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "deadline",
};

const grpc_channel_filter grpc_server_deadline_filter = {
    server_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(server_call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "deadline",
};
