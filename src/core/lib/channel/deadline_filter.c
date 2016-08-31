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
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/timer.h"

// Used for both client and server filters.
typedef struct channel_data {
} channel_data;

// Call data used for both client and server filter.
typedef struct base_call_data {
  // We take a reference to the call stack for the timer callback.
  grpc_call_stack* call_stack;
  // Guards access to timer_pending and timer.
  gpr_mu timer_mu;
  // True if the timer callback is currently pending.
  bool timer_pending;
  // The deadline timer.
  grpc_timer timer;
  // Closure to invoke when the call is complete.
  // We use this to cancel the timer.
  grpc_closure on_complete;
  // The original on_complete closure, which we chain to after our own
  // closure is invoked.
  grpc_closure* next_on_complete;
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
  gpr_mu_init(&calld->timer_mu);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.  Used for both client and server filters.
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              void* and_free_memory) {
  base_call_data* calld = elem->call_data;
  gpr_mu_destroy(&calld->timer_mu);
}

// Timer callback.
static void timer_callback(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  grpc_call_element* elem = arg;
  base_call_data* calld = elem->call_data;
  gpr_mu_lock(&calld->timer_mu);
  calld->timer_pending = false;
  gpr_mu_unlock(&calld->timer_mu);
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_call_element_send_cancel(exec_ctx, elem);
  }
  GRPC_CALL_STACK_UNREF(exec_ctx, calld->call_stack, "deadline_timer");
}

// Starts the deadline timer.
static void start_timer_if_needed(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element* elem,
                                  gpr_timespec deadline) {
  base_call_data* calld = elem->call_data;
  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);
  if (gpr_time_cmp(deadline, gpr_inf_future(GPR_CLOCK_MONOTONIC)) != 0) {
    // Take a reference to the call stack, to be owned by the timer.
    GRPC_CALL_STACK_REF(calld->call_stack, "deadline_timer");
    gpr_mu_lock(&calld->timer_mu);
    calld->timer_pending = true;
    grpc_timer_init(exec_ctx, &calld->timer, deadline, timer_callback, elem,
                    gpr_now(GPR_CLOCK_MONOTONIC));
    gpr_mu_unlock(&calld->timer_mu);
  }
}

// Cancels the deadline timer.
static void cancel_timer_if_needed(grpc_exec_ctx* exec_ctx,
                                   base_call_data* calld) {
  gpr_mu_lock(&calld->timer_mu);
  if (calld->timer_pending) {
    grpc_timer_cancel(exec_ctx, &calld->timer);
    calld->timer_pending = false;
  }
  gpr_mu_unlock(&calld->timer_mu);
}

// Callback run when the call is complete.
static void on_complete(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  base_call_data* calld = arg;
  cancel_timer_if_needed(exec_ctx, calld);
  // Invoke the next callback.
  calld->next_on_complete->cb(exec_ctx, calld->next_on_complete->cb_arg, error);
}

// Method for starting a call op for client filter.
static void client_start_transport_stream_op(grpc_exec_ctx* exec_ctx,
                                             grpc_call_element* elem,
                                             grpc_transport_stream_op* op) {
  base_call_data* calld = elem->call_data;
  // If the call is cancelled or closed, cancel the timer.
  if (op->cancel_error != GRPC_ERROR_NONE ||
      op->close_error != GRPC_ERROR_NONE) {
    cancel_timer_if_needed(exec_ctx, calld);
  } else {
    // If we're sending initial metadata, get the deadline from the metadata
    // and start the timer if needed.
    if (op->send_initial_metadata != NULL) {
      start_timer_if_needed(exec_ctx, elem,
                            op->send_initial_metadata->deadline);
    }
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    if (op->recv_trailing_metadata != NULL) {
      calld->next_on_complete = op->on_complete;
      grpc_closure_init(&calld->on_complete, on_complete, calld);
      op->on_complete = &calld->on_complete;
    }
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
  // If the call is cancelled or closed, cancel the timer.
  if (op->cancel_error != GRPC_ERROR_NONE ||
      op->close_error != GRPC_ERROR_NONE) {
    cancel_timer_if_needed(exec_ctx, &calld->base);
  } else {
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
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    // Note that we trigger this on recv_trailing_metadata, even though
    // the client never sends trailing metadata, because this is the
    // hook that tells us when the call is complete on the server side.
    if (op->recv_trailing_metadata != NULL) {
      calld->base.next_on_complete = op->on_complete;
      grpc_closure_init(&calld->base.on_complete, on_complete, calld);
      op->on_complete = &calld->base.on_complete;
    }
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
