//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/deadline/deadline_filter.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel_init.h"

//
// grpc_deadline_state
//

// The on_complete callback used when sending a cancel_error batch down the
// filter stack.  Yields the call combiner when the batch returns.
static void yield_call_combiner(void* arg, grpc_error* ignored) {
  grpc_deadline_state* deadline_state = static_cast<grpc_deadline_state*>(arg);
  GRPC_CALL_COMBINER_STOP(deadline_state->call_combiner,
                          "got on_complete from cancel_stream batch");
  GRPC_CALL_STACK_UNREF(deadline_state->call_stack, "deadline_timer");
}

// This is called via the call combiner, so access to deadline_state is
// synchronized.
static void send_cancel_op_in_call_combiner(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = grpc_make_transport_stream_op(
      GRPC_CLOSURE_INIT(&deadline_state->timer_callback, yield_call_combiner,
                        deadline_state, grpc_schedule_on_exec_ctx));
  batch->cancel_stream = true;
  batch->payload->cancel_stream.cancel_error = GRPC_ERROR_REF(error);
  elem->filter->start_transport_stream_op_batch(elem, batch);
}

// Timer callback.
static void timer_callback(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  if (error != GRPC_ERROR_CANCELLED) {
    error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Deadline Exceeded"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_DEADLINE_EXCEEDED);
    grpc_call_combiner_cancel(deadline_state->call_combiner,
                              GRPC_ERROR_REF(error));
    GRPC_CLOSURE_INIT(&deadline_state->timer_callback,
                      send_cancel_op_in_call_combiner, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_START(deadline_state->call_combiner,
                             &deadline_state->timer_callback, error,
                             "deadline exceeded -- sending cancel_stream op");
  } else {
    GRPC_CALL_STACK_UNREF(deadline_state->call_stack, "deadline_timer");
  }
}

// Starts the deadline timer.
// This is called via the call combiner, so access to deadline_state is
// synchronized.
static void start_timer_if_needed(grpc_call_element* elem,
                                  grpc_millis deadline) {
  if (deadline == GRPC_MILLIS_INF_FUTURE) {
    return;
  }
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  grpc_closure* closure = nullptr;
  switch (deadline_state->timer_state) {
    case GRPC_DEADLINE_STATE_PENDING:
      // Note: We do not start the timer if there is already a timer
      return;
    case GRPC_DEADLINE_STATE_FINISHED:
      deadline_state->timer_state = GRPC_DEADLINE_STATE_PENDING;
      // If we've already created and destroyed a timer, we always create a
      // new closure: we have no other guarantee that the inlined closure is
      // not in use (it may hold a pending call to timer_callback)
      closure =
          GRPC_CLOSURE_CREATE(timer_callback, elem, grpc_schedule_on_exec_ctx);
      break;
    case GRPC_DEADLINE_STATE_INITIAL:
      deadline_state->timer_state = GRPC_DEADLINE_STATE_PENDING;
      closure =
          GRPC_CLOSURE_INIT(&deadline_state->timer_callback, timer_callback,
                            elem, grpc_schedule_on_exec_ctx);
      break;
  }
  GPR_ASSERT(closure != nullptr);
  GRPC_CALL_STACK_REF(deadline_state->call_stack, "deadline_timer");
  grpc_timer_init(&deadline_state->timer, deadline, closure);
}

// Cancels the deadline timer.
// This is called via the call combiner, so access to deadline_state is
// synchronized.
static void cancel_timer_if_needed(grpc_deadline_state* deadline_state) {
  if (deadline_state->timer_state == GRPC_DEADLINE_STATE_PENDING) {
    deadline_state->timer_state = GRPC_DEADLINE_STATE_FINISHED;
    grpc_timer_cancel(&deadline_state->timer);
  } else {
    // timer was either in STATE_INITAL (nothing to cancel)
    // OR in STATE_FINISHED (again nothing to cancel)
  }
}

// Callback run when we receive trailing metadata.
static void recv_trailing_metadata_ready(void* arg, grpc_error* error) {
  grpc_deadline_state* deadline_state = static_cast<grpc_deadline_state*>(arg);
  cancel_timer_if_needed(deadline_state);
  // Invoke the original callback.
  GRPC_CLOSURE_RUN(deadline_state->original_recv_trailing_metadata_ready,
                   GRPC_ERROR_REF(error));
}

// Inject our own recv_trailing_metadata_ready callback into op.
static void inject_recv_trailing_metadata_ready(
    grpc_deadline_state* deadline_state, grpc_transport_stream_op_batch* op) {
  deadline_state->original_recv_trailing_metadata_ready =
      op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  GRPC_CLOSURE_INIT(&deadline_state->recv_trailing_metadata_ready,
                    recv_trailing_metadata_ready, deadline_state,
                    grpc_schedule_on_exec_ctx);
  op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &deadline_state->recv_trailing_metadata_ready;
}

// Callback and associated state for starting the timer after call stack
// initialization has been completed.
struct start_timer_after_init_state {
  bool in_call_combiner;
  grpc_call_element* elem;
  grpc_millis deadline;
  grpc_closure closure;
};
static void start_timer_after_init(void* arg, grpc_error* error) {
  struct start_timer_after_init_state* state =
      static_cast<struct start_timer_after_init_state*>(arg);
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(state->elem->call_data);
  if (!state->in_call_combiner) {
    // We are initially called without holding the call combiner, so we
    // need to bounce ourselves into it.
    state->in_call_combiner = true;
    GRPC_CALL_COMBINER_START(deadline_state->call_combiner, &state->closure,
                             GRPC_ERROR_REF(error),
                             "scheduling deadline timer");
    return;
  }
  start_timer_if_needed(state->elem, state->deadline);
  gpr_free(state);
  GRPC_CALL_COMBINER_STOP(deadline_state->call_combiner,
                          "done scheduling deadline timer");
}

void grpc_deadline_state_init(grpc_call_element* elem,
                              grpc_call_stack* call_stack,
                              grpc_call_combiner* call_combiner,
                              grpc_millis deadline) {
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  deadline_state->call_stack = call_stack;
  deadline_state->call_combiner = call_combiner;
  // Deadline will always be infinite on servers, so the timer will only be
  // set on clients with a finite deadline.
  if (deadline != GRPC_MILLIS_INF_FUTURE) {
    // When the deadline passes, we indicate the failure by sending down
    // an op with cancel_error set.  However, we can't send down any ops
    // until after the call stack is fully initialized.  If we start the
    // timer here, we have no guarantee that the timer won't pop before
    // call stack initialization is finished.  To avoid that problem, we
    // create a closure to start the timer, and we schedule that closure
    // to be run after call stack initialization is done.
    struct start_timer_after_init_state* state =
        static_cast<struct start_timer_after_init_state*>(
            gpr_zalloc(sizeof(*state)));
    state->elem = elem;
    state->deadline = deadline;
    GRPC_CLOSURE_INIT(&state->closure, start_timer_after_init, state,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_SCHED(&state->closure, GRPC_ERROR_NONE);
  }
}

void grpc_deadline_state_destroy(grpc_call_element* elem) {
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  cancel_timer_if_needed(deadline_state);
}

void grpc_deadline_state_reset(grpc_call_element* elem,
                               grpc_millis new_deadline) {
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  cancel_timer_if_needed(deadline_state);
  start_timer_if_needed(elem, new_deadline);
}

void grpc_deadline_state_client_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  if (op->cancel_stream) {
    cancel_timer_if_needed(deadline_state);
  } else {
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    if (op->recv_trailing_metadata) {
      inject_recv_trailing_metadata_ready(deadline_state, op);
    }
  }
}

//
// filter code
//

// Constructor for channel_data.  Used for both client and server filters.
static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.  Used for both client and server filters.
static void destroy_channel_elem(grpc_channel_element* elem) {}

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
static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  grpc_deadline_state_init(elem, args->call_stack, args->call_combiner,
                           args->deadline);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.  Used for both client and server filters.
static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  grpc_deadline_state_destroy(elem);
}

// Method for starting a call op for client filter.
static void client_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  grpc_deadline_state_client_start_transport_stream_op_batch(elem, op);
  // Chain to next filter.
  grpc_call_next_op(elem, op);
}

// Callback for receiving initial metadata on the server.
static void recv_initial_metadata_ready(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  server_call_data* calld = static_cast<server_call_data*>(elem->call_data);
  start_timer_if_needed(elem, calld->recv_initial_metadata->deadline);
  // Invoke the next callback.
  GRPC_CLOSURE_RUN(calld->next_recv_initial_metadata_ready,
                   GRPC_ERROR_REF(error));
}

// Method for starting a call op for server filter.
static void server_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  server_call_data* calld = static_cast<server_call_data*>(elem->call_data);
  if (op->cancel_stream) {
    cancel_timer_if_needed(&calld->base.deadline_state);
  } else {
    // If we're receiving initial metadata, we need to get the deadline
    // from the recv_initial_metadata_ready callback.  So we inject our
    // own callback into that hook.
    if (op->recv_initial_metadata) {
      calld->next_recv_initial_metadata_ready =
          op->payload->recv_initial_metadata.recv_initial_metadata_ready;
      calld->recv_initial_metadata =
          op->payload->recv_initial_metadata.recv_initial_metadata;
      GRPC_CLOSURE_INIT(&calld->recv_initial_metadata_ready,
                        recv_initial_metadata_ready, elem,
                        grpc_schedule_on_exec_ctx);
      op->payload->recv_initial_metadata.recv_initial_metadata_ready =
          &calld->recv_initial_metadata_ready;
    }
    // Make sure we know when the call is complete, so that we can cancel
    // the timer.
    // Note that we trigger this on recv_trailing_metadata, even though
    // the client never sends trailing metadata, because this is the
    // hook that tells us when the call is complete on the server side.
    if (op->recv_trailing_metadata) {
      inject_recv_trailing_metadata_ready(&calld->base.deadline_state, op);
    }
  }
  // Chain to next filter.
  grpc_call_next_op(elem, op);
}

const grpc_channel_filter grpc_client_deadline_filter = {
    client_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(base_call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,  // sizeof(channel_data)
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "deadline",
};

const grpc_channel_filter grpc_server_deadline_filter = {
    server_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(server_call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,  // sizeof(channel_data)
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "deadline",
};

bool grpc_deadline_checking_enabled(const grpc_channel_args* channel_args) {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(channel_args, GRPC_ARG_ENABLE_DEADLINE_CHECKS),
      !grpc_channel_args_want_minimal_stack(channel_args));
}

static bool maybe_add_deadline_filter(grpc_channel_stack_builder* builder,
                                      void* arg) {
  return grpc_deadline_checking_enabled(
             grpc_channel_stack_builder_get_channel_arguments(builder))
             ? grpc_channel_stack_builder_prepend_filter(
                   builder, static_cast<const grpc_channel_filter*>(arg),
                   nullptr, nullptr)
             : true;
}

void grpc_deadline_filter_init(void) {
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_deadline_filter, (void*)&grpc_client_deadline_filter);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_deadline_filter, (void*)&grpc_server_deadline_filter);
}

void grpc_deadline_filter_shutdown(void) {}
