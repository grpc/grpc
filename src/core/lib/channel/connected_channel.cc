/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/connected_channel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/transport/transport.h"

#define MAX_BUFFER_LENGTH 8192

typedef struct connected_channel_channel_data {
  grpc_transport* transport;
} channel_data;

typedef struct {
  grpc_closure closure;
  grpc_closure* original_closure;
  grpc_core::CallCombiner* call_combiner;
  const char* reason;
} callback_state;

typedef struct connected_channel_call_data {
  grpc_core::CallCombiner* call_combiner;
  // Closures used for returning results on the call combiner.
  callback_state on_complete[6];  // Max number of pending batches.
  callback_state recv_initial_metadata_ready;
  callback_state recv_message_ready;
  callback_state recv_trailing_metadata_ready;
} call_data;

static void run_in_call_combiner(void* arg, grpc_error* error) {
  callback_state* state = static_cast<callback_state*>(arg);
  GRPC_CALL_COMBINER_START(state->call_combiner, state->original_closure,
                           GRPC_ERROR_REF(error), state->reason);
}

static void run_cancel_in_call_combiner(void* arg, grpc_error* error) {
  run_in_call_combiner(arg, error);
  gpr_free(arg);
}

static void intercept_callback(call_data* calld, callback_state* state,
                               bool free_when_done, const char* reason,
                               grpc_closure** original_closure) {
  state->original_closure = *original_closure;
  state->call_combiner = calld->call_combiner;
  state->reason = reason;
  *original_closure = GRPC_CLOSURE_INIT(
      &state->closure,
      free_when_done ? run_cancel_in_call_combiner : run_in_call_combiner,
      state, grpc_schedule_on_exec_ctx);
}

static callback_state* get_state_for_batch(
    call_data* calld, grpc_transport_stream_op_batch* batch) {
  if (batch->send_initial_metadata) return &calld->on_complete[0];
  if (batch->send_message) return &calld->on_complete[1];
  if (batch->send_trailing_metadata) return &calld->on_complete[2];
  if (batch->recv_initial_metadata) return &calld->on_complete[3];
  if (batch->recv_message) return &calld->on_complete[4];
  if (batch->recv_trailing_metadata) return &calld->on_complete[5];
  GPR_UNREACHABLE_CODE(return nullptr);
}

/* We perform a small hack to locate transport data alongside the connected
   channel data in call allocations, to allow everything to be pulled in minimal
   cache line requests */
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) ((grpc_stream*)((calld) + 1))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  (((call_data*)(transport_stream)) - 1)

/* Intercept a call operation and either push it directly up or translate it
   into transport stream operations */
static void connected_channel_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (batch->recv_initial_metadata) {
    callback_state* state = &calld->recv_initial_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_initial_metadata_ready",
        &batch->payload->recv_initial_metadata.recv_initial_metadata_ready);
  }
  if (batch->recv_message) {
    callback_state* state = &calld->recv_message_ready;
    intercept_callback(calld, state, false, "recv_message_ready",
                       &batch->payload->recv_message.recv_message_ready);
  }
  if (batch->recv_trailing_metadata) {
    callback_state* state = &calld->recv_trailing_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_trailing_metadata_ready",
        &batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready);
  }
  if (batch->cancel_stream) {
    // There can be more than one cancellation batch in flight at any
    // given time, so we can't just pick out a fixed index into
    // calld->on_complete like we can for the other ops.  However,
    // cancellation isn't in the fast path, so we just allocate a new
    // closure for each one.
    callback_state* state =
        static_cast<callback_state*>(gpr_malloc(sizeof(*state)));
    intercept_callback(calld, state, true, "on_complete (cancel_stream)",
                       &batch->on_complete);
  } else if (batch->on_complete != nullptr) {
    callback_state* state = get_state_for_batch(calld, batch);
    intercept_callback(calld, state, false, "on_complete", &batch->on_complete);
  }
  grpc_transport_perform_stream_op(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld), batch);
  GRPC_CALL_COMBINER_STOP(calld->call_combiner, "passed batch to transport");
}

static void connected_channel_start_transport_op(grpc_channel_element* elem,
                                                 grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_perform_op(chand->transport, op);
}

/* Constructor for call_data */
static grpc_error* connected_channel_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  calld->call_combiner = args->call_combiner;
  int r = grpc_transport_init_stream(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld),
      &args->call_stack->refcount, args->server_transport_data, args->arena);
  return r == 0 ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "transport stream initialization failed");
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_set_pops(chand->transport,
                          TRANSPORT_STREAM_FROM_CALL_DATA(calld), pollent);
}

/* Destructor for call_data */
static void connected_channel_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* final_info,
    grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_destroy_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                then_schedule_closure);
}

/* Constructor for channel_data */
static grpc_error* connected_channel_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(args->is_last);
  cd->transport = nullptr;
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data */
static void connected_channel_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  if (cd->transport) {
    grpc_transport_destroy(cd->transport);
  }
}

/* No-op. */
static void connected_channel_get_channel_info(
    grpc_channel_element* elem, const grpc_channel_info* channel_info) {}

const grpc_channel_filter grpc_connected_filter = {
    connected_channel_start_transport_stream_op_batch,
    connected_channel_start_transport_op,
    sizeof(call_data),
    connected_channel_init_call_elem,
    set_pollset_or_pollset_set,
    connected_channel_destroy_call_elem,
    sizeof(channel_data),
    connected_channel_init_channel_elem,
    connected_channel_destroy_channel_elem,
    connected_channel_get_channel_info,
    "connected",
};

static void bind_transport(grpc_channel_stack* channel_stack,
                           grpc_channel_element* elem, void* t) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(elem->filter == &grpc_connected_filter);
  GPR_ASSERT(cd->transport == nullptr);
  cd->transport = static_cast<grpc_transport*>(t);

  /* HACK(ctiller): increase call stack size for the channel to make space
     for channel data. We need a cleaner (but performant) way to do this,
     and I'm not sure what that is yet.
     This is only "safe" because call stacks place no additional data after
     the last call element, and the last call element MUST be the connected
     channel. */
  channel_stack->call_stack_size +=
      grpc_transport_stream_size(static_cast<grpc_transport*>(t));
}

bool grpc_add_connected_filter(grpc_channel_stack_builder* builder,
                               void* arg_must_be_null) {
  GPR_ASSERT(arg_must_be_null == nullptr);
  grpc_transport* t = grpc_channel_stack_builder_get_transport(builder);
  GPR_ASSERT(t != nullptr);
  return grpc_channel_stack_builder_append_filter(
      builder, &grpc_connected_filter, bind_transport, t);
}

grpc_stream* grpc_connected_channel_get_stream(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  return TRANSPORT_STREAM_FROM_CALL_DATA(calld);
}
