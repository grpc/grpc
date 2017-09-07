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

#ifndef GRPC_CORE_EXT_FILTERS_DEADLINE_DEADLINE_FILTER_H
#define GRPC_CORE_EXT_FILTERS_DEADLINE_DEADLINE_FILTER_H

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/timer.h"

typedef enum grpc_deadline_timer_state {
  GRPC_DEADLINE_STATE_INITIAL,
  GRPC_DEADLINE_STATE_PENDING,
  GRPC_DEADLINE_STATE_FINISHED
} grpc_deadline_timer_state;

// State used for filters that enforce call deadlines.
// Must be the first field in the filter's call_data.
typedef struct grpc_deadline_state {
  // We take a reference to the call stack for the timer callback.
  grpc_call_stack* call_stack;
  grpc_call_combiner* call_combiner;
  grpc_deadline_timer_state timer_state;
  grpc_timer timer;
  grpc_closure timer_callback;
  // Closure to invoke when the call is complete.
  // We use this to cancel the timer.
  grpc_closure on_complete;
  // The original on_complete closure, which we chain to after our own
  // closure is invoked.
  grpc_closure* next_on_complete;
} grpc_deadline_state;

//
// NOTE: All of these functions require that the first field in
// elem->call_data is a grpc_deadline_state.
//

// assumes elem->call_data is zero'd
void grpc_deadline_state_init(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              grpc_call_stack* call_stack,
                              grpc_call_combiner* call_combiner,
                              gpr_timespec deadline);
void grpc_deadline_state_destroy(grpc_exec_ctx* exec_ctx,
                                 grpc_call_element* elem);

// Cancels the existing timer and starts a new one with new_deadline.
//
// Note: It is generally safe to call this with an earlier deadline
// value than the current one, but not the reverse.  No checks are done
// to ensure that the timer callback is not invoked while it is in the
// process of being reset, which means that attempting to increase the
// deadline may result in the timer being called twice.
//
// Note: Must be called while holding the call combiner.
void grpc_deadline_state_reset(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                               gpr_timespec new_deadline);

// To be called from the client-side filter's start_transport_stream_op_batch()
// method.  Ensures that the deadline timer is cancelled when the call
// is completed.
//
// Note: It is the caller's responsibility to chain to the next filter if
// necessary after this function returns.
//
// Note: Must be called while holding the call combiner.
void grpc_deadline_state_client_start_transport_stream_op_batch(
    grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
    grpc_transport_stream_op_batch* op);

// Should deadline checking be performed (according to channel args)
bool grpc_deadline_checking_enabled(const grpc_channel_args* args);

// Deadline filters for direct client channels and server channels.
// Note: Deadlines for non-direct client channels are handled by the
// client_channel filter.
extern const grpc_channel_filter grpc_client_deadline_filter;
extern const grpc_channel_filter grpc_server_deadline_filter;

#endif /* GRPC_CORE_EXT_FILTERS_DEADLINE_DEADLINE_FILTER_H */
