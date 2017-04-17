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
  gpr_atm timer_state;
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
void grpc_deadline_state_reset(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                               gpr_timespec new_deadline);

// To be called from the client-side filter's start_transport_stream_op_batch()
// method.  Ensures that the deadline timer is cancelled when the call
// is completed.
//
// Note: It is the caller's responsibility to chain to the next filter if
// necessary after this function returns.
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
