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

#include <new>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// A fire-and-forget class representing a pending deadline timer.
// Allocated on the call arena.
class TimerState {
 public:
  TimerState(grpc_call_element* elem, Timestamp deadline) : elem_(elem) {
    grpc_deadline_state* deadline_state =
        static_cast<grpc_deadline_state*>(elem_->call_data);
    GRPC_CALL_STACK_REF(deadline_state->call_stack, "DeadlineTimerState");
    GRPC_CLOSURE_INIT(&closure_, TimerCallback, this, nullptr);
    grpc_timer_init(&timer_, deadline, &closure_);
  }

  void Cancel() { grpc_timer_cancel(&timer_); }

 private:
  // The on_complete callback used when sending a cancel_error batch down the
  // filter stack.  Yields the call combiner when the batch returns.
  static void YieldCallCombiner(void* arg, grpc_error_handle /*ignored*/) {
    TimerState* self = static_cast<TimerState*>(arg);
    grpc_deadline_state* deadline_state =
        static_cast<grpc_deadline_state*>(self->elem_->call_data);
    GRPC_CALL_COMBINER_STOP(deadline_state->call_combiner,
                            "got on_complete from cancel_stream batch");
    GRPC_CALL_STACK_UNREF(deadline_state->call_stack, "DeadlineTimerState");
  }

  // This is called via the call combiner, so access to deadline_state is
  // synchronized.
  static void SendCancelOpInCallCombiner(void* arg, grpc_error_handle error) {
    TimerState* self = static_cast<TimerState*>(arg);
    grpc_transport_stream_op_batch* batch = grpc_make_transport_stream_op(
        GRPC_CLOSURE_INIT(&self->closure_, YieldCallCombiner, self, nullptr));
    batch->cancel_stream = true;
    batch->payload->cancel_stream.cancel_error = GRPC_ERROR_REF(error);
    self->elem_->filter->start_transport_stream_op_batch(self->elem_, batch);
  }

  // Timer callback.
  static void TimerCallback(void* arg, grpc_error_handle error) {
    TimerState* self = static_cast<TimerState*>(arg);
    grpc_deadline_state* deadline_state =
        static_cast<grpc_deadline_state*>(self->elem_->call_data);
    if (error != GRPC_ERROR_CANCELLED) {
      error = grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Deadline Exceeded"),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_DEADLINE_EXCEEDED);
      deadline_state->call_combiner->Cancel(GRPC_ERROR_REF(error));
      GRPC_CLOSURE_INIT(&self->closure_, SendCancelOpInCallCombiner, self,
                        nullptr);
      GRPC_CALL_COMBINER_START(deadline_state->call_combiner, &self->closure_,
                               error,
                               "deadline exceeded -- sending cancel_stream op");
    } else {
      GRPC_CALL_STACK_UNREF(deadline_state->call_stack, "DeadlineTimerState");
    }
  }

  // NOTE: This object's dtor is never called, so do not add any data
  // members that require destruction!
  // TODO(roth): We should ideally call this object's dtor somewhere,
  // but that would require adding more synchronization, because we'd
  // need to call the dtor only after both (a) the timer callback
  // finishes and (b) the filter sees the call completion and attempts
  // to cancel the timer.
  grpc_call_element* elem_;
  grpc_timer timer_;
  grpc_closure closure_;
};

}  // namespace grpc_core

//
// grpc_deadline_state
//

// Starts the deadline timer.
// This is called via the call combiner, so access to deadline_state is
// synchronized.
static void start_timer_if_needed(grpc_call_element* elem,
                                  grpc_core::Timestamp deadline) {
  if (deadline == grpc_core::Timestamp::InfFuture()) return;
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  GPR_ASSERT(deadline_state->timer_state == nullptr);
  deadline_state->timer_state =
      deadline_state->arena->New<grpc_core::TimerState>(elem, deadline);
}

// Cancels the deadline timer.
// This is called via the call combiner, so access to deadline_state is
// synchronized.
static void cancel_timer_if_needed(grpc_deadline_state* deadline_state) {
  if (deadline_state->timer_state != nullptr) {
    deadline_state->timer_state->Cancel();
    deadline_state->timer_state = nullptr;
  }
}

// Callback run when we receive trailing metadata.
static void recv_trailing_metadata_ready(void* arg, grpc_error_handle error) {
  grpc_deadline_state* deadline_state = static_cast<grpc_deadline_state*>(arg);
  cancel_timer_if_needed(deadline_state);
  // Invoke the original callback.
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          deadline_state->original_recv_trailing_metadata_ready,
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
  start_timer_after_init_state(grpc_call_element* elem,
                               grpc_core::Timestamp deadline)
      : elem(elem), deadline(deadline) {}
  ~start_timer_after_init_state() { start_timer_if_needed(elem, deadline); }

  bool in_call_combiner = false;
  grpc_call_element* elem;
  grpc_core::Timestamp deadline;
  grpc_closure closure;
};
static void start_timer_after_init(void* arg, grpc_error_handle error) {
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
  delete state;
  GRPC_CALL_COMBINER_STOP(deadline_state->call_combiner,
                          "done scheduling deadline timer");
}

grpc_deadline_state::grpc_deadline_state(grpc_call_element* elem,
                                         const grpc_call_element_args& args,
                                         grpc_core::Timestamp deadline)
    : call_stack(args.call_stack),
      call_combiner(args.call_combiner),
      arena(args.arena) {
  // Deadline will always be infinite on servers, so the timer will only be
  // set on clients with a finite deadline.
  if (deadline != grpc_core::Timestamp::InfFuture()) {
    // When the deadline passes, we indicate the failure by sending down
    // an op with cancel_error set.  However, we can't send down any ops
    // until after the call stack is fully initialized.  If we start the
    // timer here, we have no guarantee that the timer won't pop before
    // call stack initialization is finished.  To avoid that problem, we
    // create a closure to start the timer, and we schedule that closure
    // to be run after call stack initialization is done.
    struct start_timer_after_init_state* state =
        new start_timer_after_init_state(elem, deadline);
    GRPC_CLOSURE_INIT(&state->closure, start_timer_after_init, state,
                      grpc_schedule_on_exec_ctx);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &state->closure, GRPC_ERROR_NONE);
  }
}

grpc_deadline_state::~grpc_deadline_state() { cancel_timer_if_needed(this); }

void grpc_deadline_state_reset(grpc_call_element* elem,
                               grpc_core::Timestamp new_deadline) {
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
static grpc_error_handle deadline_init_channel_elem(
    grpc_channel_element* /*elem*/, grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.  Used for both client and server filters.
static void deadline_destroy_channel_elem(grpc_channel_element* /*elem*/) {}

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
static grpc_error_handle deadline_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) grpc_deadline_state(elem, *args, args->deadline);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.  Used for both client and server filters.
static void deadline_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  grpc_deadline_state* deadline_state =
      static_cast<grpc_deadline_state*>(elem->call_data);
  deadline_state->~grpc_deadline_state();
}

// Method for starting a call op for client filter.
static void deadline_client_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  grpc_deadline_state_client_start_transport_stream_op_batch(elem, op);
  // Chain to next filter.
  grpc_call_next_op(elem, op);
}

// Callback for receiving initial metadata on the server.
static void recv_initial_metadata_ready(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  server_call_data* calld = static_cast<server_call_data*>(elem->call_data);
  start_timer_if_needed(
      elem, calld->recv_initial_metadata->get(grpc_core::GrpcTimeoutMetadata())
                .value_or(grpc_core::Timestamp::InfFuture()));
  // Invoke the next callback.
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->next_recv_initial_metadata_ready,
                          GRPC_ERROR_REF(error));
}

// Method for starting a call op for server filter.
static void deadline_server_start_transport_stream_op_batch(
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
    deadline_client_start_transport_stream_op_batch,
    nullptr,
    grpc_channel_next_op,
    sizeof(base_call_data),
    deadline_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    deadline_destroy_call_elem,
    0,  // sizeof(channel_data)
    deadline_init_channel_elem,
    grpc_channel_stack_no_post_init,
    deadline_destroy_channel_elem,
    grpc_channel_next_get_info,
    "deadline",
};

const grpc_channel_filter grpc_server_deadline_filter = {
    deadline_server_start_transport_stream_op_batch,
    nullptr,
    grpc_channel_next_op,
    sizeof(server_call_data),
    deadline_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    deadline_destroy_call_elem,
    0,  // sizeof(channel_data)
    deadline_init_channel_elem,
    grpc_channel_stack_no_post_init,
    deadline_destroy_channel_elem,
    grpc_channel_next_get_info,
    "deadline",
};

bool grpc_deadline_checking_enabled(
    const grpc_core::ChannelArgs& channel_args) {
  return channel_args.GetBool(GRPC_ARG_ENABLE_DEADLINE_CHECKS)
      .value_or(!channel_args.WantMinimalStack());
}

namespace grpc_core {
void RegisterDeadlineFilter(CoreConfiguration::Builder* builder) {
  auto register_filter = [builder](grpc_channel_stack_type type,
                                   const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [filter](ChannelStackBuilder* builder) {
          auto args = builder->channel_args();
          if (grpc_deadline_checking_enabled(args)) {
            builder->PrependFilter(filter);
          }
          return true;
        });
  };
  register_filter(GRPC_CLIENT_DIRECT_CHANNEL, &grpc_client_deadline_filter);
  register_filter(GRPC_SERVER_CHANNEL, &grpc_server_deadline_filter);
}
}  // namespace grpc_core
