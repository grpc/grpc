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
// To disable the idle filte, set GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS to INT_MAX in
// channel args.
#define DEFAULT_IDLE_TIMEOUT_MS (5 /*minutes*/ * 60 * 1000)

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
  The state machine to track channel's state:

                                       IDLE
                                       |  ^
          ------------------------------  *
          |                               *
          v                               *
         BUSY ======================> LEISURE
          ^                            |  ^
          *  ---------------------------  *
          *  |                            *
          *  v                            *
  BUSY_FROM_LEISURE ===========> LEISURE_FROM_BUSY
          ^                            |
          |                            |
          ------------------------------

  ---> Triggered by IncreaseCallCount()
  ===> Triggered by DecreaseCallCount()
  ***> Triggered by IdleTimerCallback()
*/
enum ChannelState {
  // Has call: false, Timer: off, Channel IDLE: true
  CHANNEL_STATE_IDLE,
  // Has call: true,  Timer: off, Channel IDLE: false
  CHANNEL_STATE_BUSY,
  // Has call: true,  Timer: on,  Channel IDLE: false
  CHANNEL_STATE_BUSY_FROM_LEISURE,
  // Has call: false, Timer: on,  Channel IDLE: false
  // In timer callback: reset the timer.
  CHANNEL_STATE_LEISURE_FROM_BUSY,
  // Has call: false, Timer: on,  Channel IDLE: false
  // In timer callback: enter IDLE.
  CHANNEL_STATE_LEISURE
};

static grpc_millis GetClientIdleTimeout(const grpc_channel_args* args) {
  return grpc_channel_arg_get_integer(
      grpc_channel_args_find(args, GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS),
      {DEFAULT_IDLE_TIMEOUT_MS, 0, INT_MAX});
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
  static void IdleTransportOpCompleteCallback(void* arg, grpc_error* error) {
    ChannelData* chand = static_cast<ChannelData*>(arg);
    GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "idle transport op");
  }

  void StartIdleTimer() {
    GRPC_IDLE_FILTER_LOG("timer has started");
    GRPC_CHANNEL_STACK_REF(channel_stack_, "max idle timer callback");
    grpc_timer_init(&idle_timer_, last_leisure_start_time_ + max_leisure_time_,
                    &idle_timer_callback_);
  }

  void EnterIdle() {
    GRPC_IDLE_FILTER_LOG("the channel will enter IDLE");
    GRPC_CHANNEL_STACK_REF(channel_stack_, "idle transport op");
    memset(&idle_transport_op_, 0, sizeof(idle_transport_op_));
    idle_transport_op_.disconnect_with_error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("enter idle"),
        GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, GRPC_CHANNEL_IDLE);
    idle_transport_op_.on_consumed = &idle_transport_op_complete_callback_;
    grpc_channel_next_op(elem_, &idle_transport_op_);
  }

  grpc_channel_element* elem_;
  // Take a reference to the channel stack for the timer callback.
  grpc_channel_stack* channel_stack_;
  // Allowed max time a channel may have no outstanding RPCs.
  const grpc_millis max_leisure_time_;

  // Member data used to track the state of channel.
  grpc_millis last_leisure_start_time_;
  Atomic<size_t> call_count_;
  Atomic<ChannelState> state_;

  // Idle timer and its callback closure.
  grpc_timer idle_timer_;
  grpc_closure idle_timer_callback_;

  // The transport op to tell the client channel to enter IDLE.
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
  // If the op is to disconnect the channel, cancel the idle timer if it has
  // been set.
  if (op->disconnect_with_error != nullptr) {
    // Disconnect. Cancel the timer if we set it before.
    // IncreaseCallCount() introduces a dummy call and prevent the timer from
    // being reset by other threads.
    chand->IncreaseCallCount();
    // If the timer has been set, cancel the timer.
    if (chand->state_.Load(MemoryOrder::RELAXED) ==
        CHANNEL_STATE_BUSY_FROM_LEISURE) {
      // No synchronization issues here. grpc_timer_cancel() is valid as long as
      // grpc_timer_init() has been called on the given timer before.
      grpc_timer_cancel(&chand->idle_timer_);
    }
  }
  // Pass the op to the next filter.
  grpc_channel_next_op(elem, op);
}

void ChannelData::IncreaseCallCount() {
  size_t previous_value = call_count_.FetchAdd(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has increased to %" PRIuPTR,
                       previous_value + 1);
  if (previous_value == 0) {
    // If this call is the one makes the channel busy, switch the state from
    // LEISURE to BUSY.
    bool finished = false;
    // Loop here to make sure the previous decrease operation has finished.
    ChannelState state = state_.Load(MemoryOrder::RELAXED);
    while (!finished) {
      switch (state) {
        // Timer has been set. Switch to CHANNEL_STATE_BUSY_FROM_LEISURE.
        case CHANNEL_STATE_LEISURE:
        case CHANNEL_STATE_LEISURE_FROM_BUSY:
          // At this point, the state may have been switched to IDLE by the
          // idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          finished = state_.CompareExchangeWeak(
              &state, CHANNEL_STATE_BUSY_FROM_LEISURE, MemoryOrder::RELAXED,
              MemoryOrder::RELAXED);
          break;
        // Timer has not been set. Switch to CHANNEL_STATE_BUSY.
        case CHANNEL_STATE_IDLE:
          // In this case, no other threads will modify the state, so we can
          // just store the value.
          state_.Store(CHANNEL_STATE_BUSY, MemoryOrder::RELAXED);
          finished = true;
          break;
        default:
          // The state has not been switched to LEISURE/IDLE yet, try again.
          state = state_.Load(MemoryOrder::RELAXED);
          break;
      }
    }
  }
}

void ChannelData::DecreaseCallCount() {
  size_t previous_value = call_count_.FetchSub(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has decreased to %" PRIuPTR,
                       previous_value - 1);
  if (previous_value == 1) {
    // If this call is the one makes the channel leisure, switch the state from
    // BUSY to LEISURE.
    last_leisure_start_time_ = ExecCtx::Get()->Now();
    bool finished = false;
    // Loop here to make sure the previous increase operation has finished.
    ChannelState state = state_.Load(MemoryOrder::RELAXED);
    while (!finished) {
      switch (state) {
        // Timer has been set. Switch to CHANNEL_STATE_LEISURE_FROM_BUSY
        case CHANNEL_STATE_BUSY_FROM_LEISURE:
          // At this point, the state may have been switched to BUSY by the
          // idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          //
          // Release store here to make the idle timer callback see the updated
          // value of last_leisure_start_time_ to properly reset the idle timer.
          finished = state_.CompareExchangeWeak(
              &state, CHANNEL_STATE_LEISURE_FROM_BUSY, MemoryOrder::RELEASE,
              MemoryOrder::RELAXED);
          break;
        // Timer has not been set. Set the timer and switch to
        // CHANNEL_STATE_LEISURE
        case CHANNEL_STATE_BUSY:
          state_.Store(CHANNEL_STATE_LEISURE, MemoryOrder::RELAXED);
          StartIdleTimer();
          finished = true;
          break;
        default:
          // The state has not been switched to BUSY yet, try again.
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
      max_leisure_time_(GetClientIdleTimeout(args->channel_args)),
      call_count_(0),
      state_(CHANNEL_STATE_IDLE) {
  // If the idle filter is explicitly disabled in channel args, this ctor should
  // not get called.
  GPR_ASSERT(max_leisure_time_ != GRPC_MILLIS_INF_FUTURE);
  GRPC_IDLE_FILTER_LOG("created with max_leisure_time = %" PRId64 " ms",
                       max_leisure_time_);
  // Initialize idle timer callback closure.
  GRPC_CLOSURE_INIT(&idle_timer_callback_, IdleTimerCallback, this,
                    grpc_schedule_on_exec_ctx);
  // Initialize the idle transport op.
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
      case CHANNEL_STATE_BUSY_FROM_LEISURE:
        finished = chand->state_.CompareExchangeWeak(&state, CHANNEL_STATE_BUSY,
                                                     MemoryOrder::RELAXED,
                                                     MemoryOrder::RELAXED);
        break;
      case CHANNEL_STATE_LEISURE_FROM_BUSY:
        finished = chand->state_.CompareExchangeWeak(
            &state, CHANNEL_STATE_LEISURE, MemoryOrder::ACQUIRE,
            MemoryOrder::RELAXED);
        if (finished) {
          chand->StartIdleTimer();
        }
        break;
      case CHANNEL_STATE_LEISURE:
        finished = chand->state_.CompareExchangeWeak(&state, CHANNEL_STATE_IDLE,
                                                     MemoryOrder::RELAXED,
                                                     MemoryOrder::RELAXED);
        if (finished) {
          chand->EnterIdle();
        }
        break;
      default:
        // The state has not been set properly yet, try again.
        chand->state_.Load(MemoryOrder::RELAXED);
        break;
    }
  }
  GRPC_IDLE_FILTER_LOG("timer finished");
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
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

static bool maybe_add_client_idle_filter(grpc_channel_stack_builder* builder,
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
      grpc_core::maybe_add_client_idle_filter, nullptr);
}

void grpc_client_idle_filter_shutdown(void) {}
