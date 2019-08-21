/*
 *
 * Copyright 2019 gRPC authors.
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

#include <limits.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/http2_errors.h"

// The idle filter is enabled in client channel by default.
// Set GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS to [1000, INT_MAX) in channel args to
// configure the idle timeout.
#define DEFAULT_IDLE_TIMEOUT_MS (30 /*minutes*/ * 60 * 1000)
// The user input idle timeout smaller than this would be capped to it.
#define MIN_IDLE_TIMEOUT_MS (1 /*second*/ * 1000)

namespace grpc_core {

TraceFlag grpc_trace_client_idle_filter(false, "client_idle_filter");

#define GRPC_IDLE_FILTER_LOG(format, ...)                               \
  do {                                                                  \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_client_idle_filter)) {       \
      gpr_log(GPR_INFO, "(client idle filter) " format, ##__VA_ARGS__); \
    }                                                                   \
  } while (0)

namespace {

/*
  client_idle_filter maintains a state tracking if there are active calls in the
  channel and its internal idle_timer_. The states are specified as following:

  +--------------------------------------------+-------------+---------+
  |               ChannelState                 | idle_timer_ | channel |
  +--------------------------------------------+-------------+---------+
  | IDLE                                       | unset       | idle    |
  | CALLS_ACTIVE                               | unset       | busy    |
  | TIMER_PENDING                              | set-valid   | idle    |
  | TIMER_PENDING_CALLS_ACTIVE                 | set-invalid | busy    |
  | TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START | set-invalid | idle    |
  +--------------------------------------------+-------------+---------+

  IDLE: The initial state of the client_idle_filter, indicating the channel is
  in IDLE.

  CALLS_ACTIVE: The channel has 1 or 1+ active calls and the timer is not set.

  TIMER_PENDING: The state after the timer is set and no calls have arrived
  after the timer is set. The channel must have 0 active call in this state. If
  the timer is fired in this state, the channel will go into IDLE state.

  TIMER_PENDING_CALLS_ACTIVE: The state after the timer is set and at least one
  call has arrived after the timer is set. The channel must have 1 or 1+ active
  calls in this state. If the timer is fired in this state, we won't reschedule
  it.

  TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START: The state after the timer is set
  and at least one call has arrived after the timer is set, BUT the channel
  currently has 0 active call. If the timer is fired in this state, we will
  reschedule it according to the finish time of the latest call.

  PROCESSING: The state set to block other threads when the setting thread is
  doing some work to keep state consistency.

  idle_timer_ will not be cancelled (unless the channel is shutting down).
  If the timer callback is called when the idle_timer_ is valid (i.e. idle_state
  is TIMER_PENDING), the channel will enter IDLE, otherwise the channel won't be
  changed.

  State transitions:
                                            IDLE
                                            |  ^
            ---------------------------------  *
            |                                  *
            v                                  *
      CALLS_ACTIVE =================> TIMER_PENDING
            ^                               |  ^
            *  ------------------------------  *
            *  |                               *
            *  v                               *
TIMER_PENDING_CALLS_ACTIVE ===> TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START
            ^                               |
            |                               |
            ---------------------------------

  ---> Triggered by IncreaseCallCount()
  ===> Triggered by DecreaseCallCount()
  ***> Triggered by IdleTimerCallback()
*/
enum ChannelState {
  IDLE,
  CALLS_ACTIVE,
  TIMER_PENDING,
  TIMER_PENDING_CALLS_ACTIVE,
  TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START,
  PROCESSING
};

grpc_millis GetClientIdleTimeout(const grpc_channel_args* args) {
  return GPR_MAX(
      grpc_channel_arg_get_integer(
          grpc_channel_args_find(args, GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS),
          {DEFAULT_IDLE_TIMEOUT_MS, 0, INT_MAX}),
      MIN_IDLE_TIMEOUT_MS);
}

class ChannelData {
 public:
  static grpc_error* Init(grpc_channel_element* elem,
                          grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op);

  void IncreaseCallCount();

  void DecreaseCallCount();

 private:
  ChannelData(grpc_channel_element* elem, grpc_channel_element_args* args,
              grpc_error** error);
  ~ChannelData() = default;

  static void IdleTimerCallback(void* arg, grpc_error* error);
  static void IdleTransportOpCompleteCallback(void* arg, grpc_error* error);

  void StartIdleTimer();

  void EnterIdle();

  grpc_channel_element* elem_;
  // The channel stack to which we take refs for pending callbacks.
  grpc_channel_stack* channel_stack_;
  // Timeout after the last RPC finishes on the client channel at which the
  // channel goes back into IDLE state.
  const grpc_millis client_idle_timeout_;

  // Member data used to track the state of channel.
  grpc_millis last_idle_time_;
  Atomic<intptr_t> call_count_{0};
  Atomic<ChannelState> state_{IDLE};

  // Idle timer and its callback closure.
  grpc_timer idle_timer_;
  grpc_closure idle_timer_callback_;

  // The transport op telling the client channel to enter IDLE.
  grpc_transport_op idle_transport_op_;
  grpc_closure idle_transport_op_complete_callback_;
};

grpc_error* ChannelData::Init(grpc_channel_element* elem,
                              grpc_channel_element_args* args) {
  grpc_error* error = GRPC_ERROR_NONE;
  new (elem->channel_data) ChannelData(elem, args, &error);
  return error;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

void ChannelData::StartTransportOp(grpc_channel_element* elem,
                                   grpc_transport_op* op) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  // Catch the disconnect_with_error transport op.
  if (op->disconnect_with_error != nullptr) {
    // IncreaseCallCount() introduces a dummy call and prevent the timer from
    // being reset by other threads.
    chand->IncreaseCallCount();
    // If the timer has been set, cancel the timer.
    // No synchronization issues here. grpc_timer_cancel() is valid as long as
    // the timer has been init()ed before.
    grpc_timer_cancel(&chand->idle_timer_);
  }
  // Pass the op to the next filter.
  grpc_channel_next_op(elem, op);
}

void ChannelData::IncreaseCallCount() {
  const intptr_t previous_value = call_count_.FetchAdd(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has increased to %" PRIuPTR,
                       previous_value + 1);
  if (previous_value == 0) {
    // This call is the one that makes the channel busy.
    // Loop here to make sure the previous decrease operation has finished.
    ChannelState state = state_.Load(MemoryOrder::RELAXED);
    while (true) {
      switch (state) {
        // Timer has not been set. Switch to CALLS_ACTIVE.
        case IDLE:
          // In this case, no other threads will modify the state, so we can
          // just store the value.
          state_.Store(CALLS_ACTIVE, MemoryOrder::RELAXED);
          return;
        // Timer has been set. Switch to TIMER_PENDING_CALLS_ACTIVE.
        case TIMER_PENDING:
        case TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START:
          // At this point, the state may have been switched to IDLE by the
          // idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          // Use MemoryOrder::ACQUIRE on success to ensure last_idle_time_ has
          // been properly set in DecreaseCallCount().
          if (state_.CompareExchangeWeak(&state, TIMER_PENDING_CALLS_ACTIVE,
                                         MemoryOrder::ACQUIRE,
                                         MemoryOrder::RELAXED)) {
            return;
          }
          break;
        default:
          // The state has not been switched to desired value yet, try again.
          state = state_.Load(MemoryOrder::RELAXED);
          break;
      }
    }
  }
}

void ChannelData::DecreaseCallCount() {
  const intptr_t previous_value = call_count_.FetchSub(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has decreased to %" PRIuPTR,
                       previous_value - 1);
  if (previous_value == 1) {
    // This call is the one that makes the channel idle.
    // last_idle_time_ does not need to be Atomic<> because busy-loops in
    // IncreaseCallCount(), DecreaseCallCount() and IdleTimerCallback() will
    // prevent multiple threads from simultaneously accessing this variable.
    last_idle_time_ = ExecCtx::Get()->Now();
    ChannelState state = state_.Load(MemoryOrder::RELAXED);
    while (true) {
      switch (state) {
        // Timer has not been set. Set the timer and switch to TIMER_PENDING
        case CALLS_ACTIVE:
          // Release store here to make other threads see the updated value of
          // last_idle_time_.
          StartIdleTimer();
          state_.Store(TIMER_PENDING, MemoryOrder::RELEASE);
          return;
        // Timer has been set. Switch to
        // TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START
        case TIMER_PENDING_CALLS_ACTIVE:
          // At this point, the state may have been switched to CALLS_ACTIVE by
          // the idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          // Release store here to make the idle timer callback see the updated
          // value of last_idle_time_ to properly reset the idle timer.
          if (state_.CompareExchangeWeak(
                  &state, TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START,
                  MemoryOrder::RELEASE, MemoryOrder::RELAXED)) {
            return;
          }
          break;
        default:
          // The state has not been switched to desired value yet, try again.
          state = state_.Load(MemoryOrder::RELAXED);
          break;
      }
    }
  }
}

ChannelData::ChannelData(grpc_channel_element* elem,
                         grpc_channel_element_args* args, grpc_error** error)
    : elem_(elem),
      channel_stack_(args->channel_stack),
      client_idle_timeout_(GetClientIdleTimeout(args->channel_args)) {
  // If the idle filter is explicitly disabled in channel args, this ctor should
  // not get called.
  GPR_ASSERT(client_idle_timeout_ != GRPC_MILLIS_INF_FUTURE);
  GRPC_IDLE_FILTER_LOG("created with max_leisure_time = %" PRId64 " ms",
                       client_idle_timeout_);
  // Initialize the idle timer without setting it.
  grpc_timer_init_unset(&idle_timer_);
  // Initialize the idle timer callback closure.
  GRPC_CLOSURE_INIT(&idle_timer_callback_, IdleTimerCallback, this,
                    grpc_schedule_on_exec_ctx);
  // Initialize the idle transport op complete callback.
  GRPC_CLOSURE_INIT(&idle_transport_op_complete_callback_,
                    IdleTransportOpCompleteCallback, this,
                    grpc_schedule_on_exec_ctx);
}

void ChannelData::IdleTimerCallback(void* arg, grpc_error* error) {
  GRPC_IDLE_FILTER_LOG("timer alarms");
  ChannelData* chand = static_cast<ChannelData*>(arg);
  if (error != GRPC_ERROR_NONE) {
    GRPC_IDLE_FILTER_LOG("timer canceled");
    GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
    return;
  }
  bool finished = false;
  ChannelState state = chand->state_.Load(MemoryOrder::RELAXED);
  while (!finished) {
    switch (state) {
      case TIMER_PENDING:
        // Change the state to PROCESSING to block IncreaseCallCout() until the
        // EnterIdle() operation finishes, preventing mistakenly entering IDLE
        // when active RPC exists.
        finished = chand->state_.CompareExchangeWeak(
            &state, PROCESSING, MemoryOrder::ACQUIRE, MemoryOrder::RELAXED);
        if (finished) {
          chand->EnterIdle();
          chand->state_.Store(IDLE, MemoryOrder::RELAXED);
        }
        break;
      case TIMER_PENDING_CALLS_ACTIVE:
        finished = chand->state_.CompareExchangeWeak(
            &state, CALLS_ACTIVE, MemoryOrder::RELAXED, MemoryOrder::RELAXED);
        break;
      case TIMER_PENDING_CALLS_SEEN_SINCE_TIMER_START:
        // Change the state to PROCESSING to block IncreaseCallCount() until the
        // StartIdleTimer() operation finishes, preventing mistakenly restarting
        // the timer after grpc_timer_cancel() when shutdown.
        finished = chand->state_.CompareExchangeWeak(
            &state, PROCESSING, MemoryOrder::ACQUIRE, MemoryOrder::RELAXED);
        if (finished) {
          chand->StartIdleTimer();
          chand->state_.Store(TIMER_PENDING, MemoryOrder::RELAXED);
        }
        break;
      default:
        // The state has not been switched to desired value yet, try again.
        state = chand->state_.Load(MemoryOrder::RELAXED);
        break;
    }
  }
  GRPC_IDLE_FILTER_LOG("timer finishes");
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
}

void ChannelData::IdleTransportOpCompleteCallback(void* arg,
                                                  grpc_error* error) {
  ChannelData* chand = static_cast<ChannelData*>(arg);
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "idle transport op");
}

void ChannelData::StartIdleTimer() {
  GRPC_IDLE_FILTER_LOG("timer has started");
  // Hold a ref to the channel stack for the timer callback.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "max idle timer callback");
  grpc_timer_init(&idle_timer_, last_idle_time_ + client_idle_timeout_,
                  &idle_timer_callback_);
}

void ChannelData::EnterIdle() {
  GRPC_IDLE_FILTER_LOG("the channel will enter IDLE");
  // Hold a ref to the channel stack for the transport op.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "idle transport op");
  // Initialize the transport op.
  idle_transport_op_ = {};
  idle_transport_op_.disconnect_with_error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("enter idle"),
      GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, GRPC_CHANNEL_IDLE);
  idle_transport_op_.on_consumed = &idle_transport_op_complete_callback_;
  // Pass the transport op down to the channel stack.
  grpc_channel_next_op(elem_, &idle_transport_op_);
}

class CallData {
 public:
  static grpc_error* Init(grpc_call_element* elem,
                          const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
};

grpc_error* CallData::Init(grpc_call_element* elem,
                           const grpc_call_element_args* args) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->IncreaseCallCount();
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* final_info,
                       grpc_closure* ignored) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->DecreaseCallCount();
}

const grpc_channel_filter grpc_client_idle_filter = {
    grpc_call_next_op,
    ChannelData::StartTransportOp,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(ChannelData),
    ChannelData::Init,
    ChannelData::Destroy,
    grpc_channel_next_get_info,
    "client_idle"};

static bool MaybeAddClientIdleFilter(grpc_channel_stack_builder* builder,
                                     void* arg) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (!grpc_channel_args_want_minimal_stack(channel_args) &&
      GetClientIdleTimeout(channel_args) != INT_MAX) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, &grpc_client_idle_filter, nullptr, nullptr);
  } else {
    return true;
  }
}

}  // namespace
}  // namespace grpc_core

void grpc_client_idle_filter_init(void) {
  grpc_channel_init_register_stage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      grpc_core::MaybeAddClientIdleFilter, nullptr);
}

void grpc_client_idle_filter_shutdown(void) {}
