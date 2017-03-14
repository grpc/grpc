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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"

//
// grpc_deadline_state
//

// Timer callback.
static void timer_callback(grpc_exec_ctx* exec_ctx, void* arg,
                           grpc_error* error) {
  grpc_call_element* elem = arg;
  grpc_deadline_state* deadline_state = elem->call_data;
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_call_element_signal_error(
        exec_ctx, elem,
        grpc_error_set_int(GRPC_ERROR_CREATE("Deadline Exceeded"),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_DEADLINE_EXCEEDED));
  }
  GRPC_CALL_STACK_UNREF(exec_ctx, deadline_state->call_stack, "deadline_timer");
}

// Starts the deadline timer.
static void start_timer_if_needed(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  gpr_timespec deadline) {
  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);
  if (gpr_time_cmp(deadline, gpr_inf_future(GPR_CLOCK_MONOTONIC)) == 0) {
    return;
  }
  grpc_deadline_state* deadline_state = elem->call_data;
  grpc_deadline_timer_state cur_state;
  grpc_closure* closure = NULL;
retry:
  cur_state =
      (grpc_deadline_timer_state)gpr_atm_acq_load(&deadline_state->timer_state);
  switch (cur_state) {
    case GRPC_DEADLINE_STATE_PENDING:
      // Note: We do not start the timer if there is already a timer
      return;
    case GRPC_DEADLINE_STATE_FINISHED:
      if (gpr_atm_rel_cas(&deadline_state->timer_state,
                          GRPC_DEADLINE_STATE_FINISHED,
                          GRPC_DEADLINE_STATE_PENDING)) {
        // If we've already created and destroyed a timer, we always create a
        // new closure: we have no other guarantee that the inlined closure is
        // not in use (it may hold a pending call to timer_callback)
        closure = grpc_closure_create(timer_callback, elem,
                                      grpc_schedule_on_exec_ctx);
      } else {
        goto retry;
      }
      break;
    case GRPC_DEADLINE_STATE_INITIAL:
      if (gpr_atm_rel_cas(&deadline_state->timer_state,
                          GRPC_DEADLINE_STATE_INITIAL,
                          GRPC_DEADLINE_STATE_PENDING)) {
        closure =
            grpc_closure_init(&deadline_state->timer_callback, timer_callback,
                              elem, grpc_schedule_on_exec_ctx);
      } else {
        goto retry;
      }
      break;
  }
  GPR_ASSERT(closure);
  GRPC_CALL_STACK_REF(deadline_state->call_stack, "deadline_timer");
  grpc_timer_init(exec_ctx, &deadline_state->timer, deadline, closure,
                  gpr_now(GPR_CLOCK_MONOTONIC));
}

// Cancels the deadline timer.
static void cancel_timer_if_needed(grpc_exec_ctx* exec_ctx,
                                   grpc_deadline_state* deadline_state) {
  if (gpr_atm_rel_cas(&deadline_state->timer_state, GRPC_DEADLINE_STATE_PENDING,
                      GRPC_DEADLINE_STATE_FINISHED)) {
    grpc_timer_cancel(exec_ctx, &deadline_state->timer);
  } else {
    // timer was either in STATE_INITAL (nothing to cancel)
    // OR in STATE_FINISHED (again nothing to cancel)
  }
}

// Callback run when the call is complete.
static void on_complete(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  grpc_deadline_state* deadline_state = arg;
  cancel_timer_if_needed(exec_ctx, deadline_state);
  // Invoke the next callback.
  grpc_closure_run(exec_ctx, deadline_state->next_on_complete,
                   GRPC_ERROR_REF(error));
}

// Inject our own on_complete callback into op.
static void inject_on_complete_cb(grpc_deadline_state* deadline_state,
                                  grpc_transport_stream_op* op) {
  deadline_state->next_on_complete = op->on_complete;
  grpc_closure_init(&deadline_state->on_complete, on_complete, deadline_state,
                    grpc_schedule_on_exec_ctx);
  op->on_complete = &deadline_state->on_complete;
}

void grpc_deadline_state_init(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              grpc_call_stack* call_stack) {
  grpc_deadline_state* deadline_state = elem->call_data;
  deadline_state->call_stack = call_stack;
}

void grpc_deadline_state_destroy(grpc_exec_ctx* exec_ctx,
                                 grpc_call_element* elem) {
  grpc_deadline_state* deadline_state = elem->call_data;
  cancel_timer_if_needed(exec_ctx, deadline_state);
}

// Callback and associated state for starting the timer after call stack
// initialization has been completed.
struct start_timer_after_init_state {
  grpc_call_element* elem;
  gpr_timespec deadline;
  grpc_closure closure;
};
static void start_timer_after_init(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
  struct start_timer_after_init_state* state = arg;
  start_timer_if_needed(exec_ctx, state->elem, state->deadline);
  gpr_free(state);
}

void grpc_deadline_state_start(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                               gpr_timespec deadline) {
  // Deadline will always be infinite on servers, so the timer will only be
  // set on clients with a finite deadline.
  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);
  if (gpr_time_cmp(deadline, gpr_inf_future(GPR_CLOCK_MONOTONIC)) != 0) {
    // When the deadline passes, we indicate the failure by sending down
    // an op with cancel_error set.  However, we can't send down any ops
    // until after the call stack is fully initialized.  If we start the
    // timer here, we have no guarantee that the timer won't pop before
    // call stack initialization is finished.  To avoid that problem, we
    // create a closure to start the timer, and we schedule that closure
    // to be run after call stack initialization is done.
    struct start_timer_after_init_state* state = gpr_malloc(sizeof(*state));
    state->elem = elem;
    state->deadline = deadline;
    grpc_closure_init(&state->closure, start_timer_after_init, state,
                      grpc_schedule_on_exec_ctx);
    grpc_closure_sched(exec_ctx, &state->closure, GRPC_ERROR_NONE);
  }
}

void grpc_deadline_state_reset(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                               gpr_timespec new_deadline) {
  grpc_deadline_state* deadline_state = elem->call_data;
  cancel_timer_if_needed(exec_ctx, deadline_state);
  start_timer_if_needed(exec_ctx, elem, new_deadline);
}

void grpc_deadline_state_client_start_transport_stream_op(
    grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
    grpc_transport_stream_op* op) {
  grpc_deadline_state* deadline_state = elem->call_data;
  if (op->cancel_error != GRPC_ERROR_NONE) {
    cancel_timer_if_needed(exec_ctx, deadline_state);
  } else {
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    if (op->recv_trailing_metadata != NULL) {
      inject_on_complete_cb(deadline_state, op);
    }
  }
}

//
// filter code
//

// Constructor for channel_data.  Used for both client and server filters.
static grpc_error* init_channel_elem(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.  Used for both client and server filters.
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {}

// Call data used for both client and server filter.
typedef struct base_call_data {
  grpc_deadline_state deadline_state;
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

// Constructor for call_data.  Used for both client and server filters.
static grpc_error* init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  grpc_deadline_state_init(exec_ctx, elem, args->call_stack);
  grpc_deadline_state_start(exec_ctx, elem, args->deadline);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.  Used for both client and server filters.
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              void* and_free_memory) {
  grpc_deadline_state_destroy(exec_ctx, elem);
}

// Method for starting a call op for client filter.
static void client_start_transport_stream_op(grpc_exec_ctx* exec_ctx,
                                             grpc_call_element* elem,
                                             grpc_transport_stream_op* op) {
  grpc_deadline_state_client_start_transport_stream_op(exec_ctx, elem, op);
  // Chain to next filter.
  grpc_call_next_op(exec_ctx, elem, op);
}

// Callback for receiving initial metadata on the server.
static void recv_initial_metadata_ready(grpc_exec_ctx* exec_ctx, void* arg,
                                        grpc_error* error) {
  grpc_call_element* elem = arg;
  server_call_data* calld = elem->call_data;
  // Get deadline from metadata and start the timer if needed.
  start_timer_if_needed(exec_ctx, elem, calld->recv_initial_metadata->deadline);
  // Invoke the next callback.
  calld->next_recv_initial_metadata_ready->cb(
      exec_ctx, calld->next_recv_initial_metadata_ready->cb_arg, error);
}

// Method for starting a call op for server filter.
static void server_start_transport_stream_op(grpc_exec_ctx* exec_ctx,
                                             grpc_call_element* elem,
                                             grpc_transport_stream_op* op) {
  server_call_data* calld = elem->call_data;
  if (op->cancel_error != GRPC_ERROR_NONE) {
    cancel_timer_if_needed(exec_ctx, &calld->base.deadline_state);
  } else {
    // If we're receiving initial metadata, we need to get the deadline
    // from the recv_initial_metadata_ready callback.  So we inject our
    // own callback into that hook.
    if (op->recv_initial_metadata_ready != NULL) {
      calld->next_recv_initial_metadata_ready = op->recv_initial_metadata_ready;
      calld->recv_initial_metadata = op->recv_initial_metadata;
      grpc_closure_init(&calld->recv_initial_metadata_ready,
                        recv_initial_metadata_ready, elem,
                        grpc_schedule_on_exec_ctx);
      op->recv_initial_metadata_ready = &calld->recv_initial_metadata_ready;
    }
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    // Note that we trigger this on recv_trailing_metadata, even though
    // the client never sends trailing metadata, because this is the
    // hook that tells us when the call is complete on the server side.
    if (op->recv_trailing_metadata != NULL) {
      inject_on_complete_cb(&calld->base.deadline_state, op);
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
    0,  // sizeof(channel_data)
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "deadline",
};

const grpc_channel_filter grpc_server_deadline_filter = {
    server_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(server_call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,  // sizeof(channel_data)
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "deadline",
};
