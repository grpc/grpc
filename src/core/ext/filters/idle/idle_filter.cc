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

#include "src/core/ext/filters/idle/idle_filter.h"

#include <limits.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/http2_errors.h"

// The idle filter is enabled in client channel by default.
// To disable the idle filte, set GRPC_ARG_MAX_CONNECTION_IDLE_MS to INT_MAX in
// channel args.
#define DEFAULT_MAX_LEISURE_TIME_MS (5 /*minutes*/ * 60 * 1000)

namespace {

grpc_core::DebugOnlyTraceFlag grpc_trace_idle_filter(false, "idle_filter");

#define GRPC_IDLE_FILTER_LOG(format, ...)                        \
  do {                                                           \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_idle_filter)) {       \
      gpr_log(GPR_INFO, "(idle filter) " format, ##__VA_ARGS__); \
    }                                                            \
  } while (0)

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
  // Busy:         false
  // Timer is on:  false
  // Channel IDLE: true
  CHANNEL_STATE_IDLE,
  // Busy:         true
  // Timer is on:  false
  // Channel IDLE: false
  CHANNEL_STATE_BUSY,
  // Busy:         true
  // Timer is on:  true
  // Channel IDLE: false
  CHANNEL_STATE_BUSY_FROM_LEISURE,
  // Busy:         false
  // Timer is on:  true (need reset the timer)
  // Channel IDLE: false
  CHANNEL_STATE_LEISURE_FROM_BUSY,
  // Busy:         false
  // Timer is on:  true (need not reset the timer)
  // Channel IDLE: false
  CHANNEL_STATE_LEISURE
};

class CallData {
 public:
  static grpc_error* Init(grpc_call_element* elem,
                          const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
};

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
  class ConnectivityWatcherSetter;

  ChannelData(grpc_channel_element_args* args, grpc_error** error);

  static void IdleTimerCallback(void* arg, grpc_error* error);

  void StartIdleTimer() {
    GRPC_IDLE_FILTER_LOG("timer has started");
    GRPC_CHANNEL_STACK_REF(channel_stack_, "max idle timer callback");
    grpc_timer_init(&idle_timer_, last_leisure_start_time_ + max_leisure_time_,
                    &idle_timer_callback_);
  }

  void EnterIdle() {
    GRPC_IDLE_FILTER_LOG("the channel will enter IDLE");
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("enter idle"),
        GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, GRPC_CHANNEL_IDLE);
    grpc_channel_element* elem = grpc_channel_stack_element(channel_stack_, 0);
    elem->filter->start_transport_op(elem, op);
  }

  // Take a reference to the channel stack for the timer callback.
  grpc_channel_stack* channel_stack_;
  // Allowed max time a channel may have no outstanding rpcs.
  grpc_millis max_leisure_time_;

  // Member data used to track the state of channel.
  grpc_millis last_leisure_start_time_;
  grpc_core::Atomic<size_t> call_count_;
  grpc_core::Atomic<ChannelState> state_;

  // Idle timer and its callback closure.
  grpc_timer idle_timer_;
  grpc_closure idle_timer_callback_;
};

ChannelData::ChannelData(grpc_channel_element_args* args, grpc_error** error)
    : channel_stack_(args->channel_stack),
      max_leisure_time_(DEFAULT_MAX_LEISURE_TIME_MS),
      call_count_(0),
      state_(CHANNEL_STATE_IDLE) {
  // Get the max_leisure_time_ from channel args, or use the default value.
  auto arg = grpc_channel_args_find(args->channel_args,
                                    GRPC_ARG_MAX_CONNECTION_IDLE_MS);
  if (arg != nullptr) {
    const int value = grpc_channel_arg_get_integer(arg, {INT_MAX, 0, INT_MAX});
    max_leisure_time_ = value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
  }
  // If the idle filter is explicitly disabled in channel args, this ctor should
  // not get called.
  GPR_ASSERT(max_leisure_time_ != GRPC_MILLIS_INF_FUTURE);
  GRPC_IDLE_FILTER_LOG("created with max_leisure_time = %" PRId64 " ms",
                       max_leisure_time_);
  // Initialize idle timer callback closure.
  GRPC_CLOSURE_INIT(&idle_timer_callback_, IdleTimerCallback, this,
                    grpc_schedule_on_exec_ctx);
}

void ChannelData::IncreaseCallCount() {
  size_t previous_value =
      call_count_.FetchAdd(1, grpc_core::MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has increased to %" PRIuPTR,
                       previous_value + 1);
  if (previous_value == 0) {
    // If this call is the one makes the channel busy, switch the state from
    // LEISURE to BUSY.
    bool finished = false;
    // Loop here to make sure the previous decrease operation has finished.
    ChannelState state = state_.Load(grpc_core::MemoryOrder::RELAXED);
    while (!finished) {
      switch (state) {
        // Timer has been set. Switch to CHANNEL_STATE_BUSY_FROM_LEISURE.
        case CHANNEL_STATE_LEISURE:
        case CHANNEL_STATE_LEISURE_FROM_BUSY:
          // At this point, the sate may have been switched to IDLE by the
          // idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          finished = state_.CompareExchangeWeak(
              &state, CHANNEL_STATE_BUSY_FROM_LEISURE,
              grpc_core::MemoryOrder::RELAXED, grpc_core::MemoryOrder::RELAXED);
          break;
        // Timer has not been set. Switch to CHANNEL_STATE_BUSY.
        case CHANNEL_STATE_IDLE:
          // In this case, no other threads will modify the state, so we can
          // just store the value.
          state_.Store(CHANNEL_STATE_BUSY, grpc_core::MemoryOrder::RELAXED);
          finished = true;
          break;
        default:
          // The state has not been switched to LEISURE/IDLE yet, try again.
          state = state_.Load(grpc_core::MemoryOrder::RELAXED);
          break;
      }
    }
  }
}

void ChannelData::DecreaseCallCount() {
  size_t previous_value =
      call_count_.FetchSub(1, grpc_core::MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has decreased to %" PRIuPTR,
                       previous_value - 1);
  if (previous_value == 1) {
    // If this call is the one makes the channel leisure, switch the state from
    // BUSY to LEISURE.
    last_leisure_start_time_ = grpc_core::ExecCtx::Get()->Now();
    bool finished = false;
    // Loop here to make sure the previous increase operation has finished.
    ChannelState state = state_.Load(grpc_core::MemoryOrder::RELAXED);
    while (!finished) {
      switch (state) {
        // Timer has been set. Switch to CHANNEL_STATE_LEISURE_FROM_BUSY
        case CHANNEL_STATE_BUSY_FROM_LEISURE:
          // At this point, the sate may have been switched to BUSY by the
          // idle timer callback. Therefore, use CAS operation to change the
          // state atomically.
          //
          // Release store here to make the idle timer
          // callback see the updated value of last_leisure_start_time_ to
          // properly reset the idle timer.
          finished = state_.CompareExchangeWeak(
              &state, CHANNEL_STATE_LEISURE_FROM_BUSY,
              grpc_core::MemoryOrder::RELEASE, grpc_core::MemoryOrder::RELAXED);
          break;
        // Timer has not been set. Set the timer and switch to
        // CHANNEL_STATE_LEISURE
        case CHANNEL_STATE_BUSY:
          StartIdleTimer();
          state_.Store(CHANNEL_STATE_LEISURE, grpc_core::MemoryOrder::RELAXED);
          finished = true;
          break;
        default:
          // The state has not been switched to BUSY yet, try again.
          state = state_.Load(grpc_core::MemoryOrder::RELAXED);
          break;
      }
    }
  }
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
  ChannelState state = chand->state_.Load(grpc_core::MemoryOrder::RELAXED);
  while (!finished) {
    switch (state) {
      case CHANNEL_STATE_BUSY_FROM_LEISURE:
        finished = chand->state_.CompareExchangeWeak(
            &state, CHANNEL_STATE_BUSY, grpc_core::MemoryOrder::RELAXED,
            grpc_core::MemoryOrder::RELAXED);
        break;
      case CHANNEL_STATE_LEISURE_FROM_BUSY:
        finished = chand->state_.CompareExchangeWeak(
            &state, CHANNEL_STATE_LEISURE, grpc_core::MemoryOrder::ACQUIRE,
            grpc_core::MemoryOrder::RELAXED);
        if (finished) {
          chand->StartIdleTimer();
        }
        break;
      case CHANNEL_STATE_LEISURE:
        finished = chand->state_.CompareExchangeWeak(
            &state, CHANNEL_STATE_IDLE, grpc_core::MemoryOrder::RELAXED,
            grpc_core::MemoryOrder::RELAXED);
        if (finished) {
          chand->EnterIdle();
        }
        break;
      default:
        // The state has not been set properly yet, try again.
        chand->state_.Load(grpc_core::MemoryOrder::RELAXED);
        break;
    }
  }
  GRPC_IDLE_FILTER_LOG("timer finished");
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
}

void ChannelData::StartTransportOp(grpc_channel_element* elem,
                                   grpc_transport_op* op) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  intptr_t value;
  // Catch the disconnect_with_error transport op.
  // If the op is to disconnect the channel, cancel the idle timer if it has
  // been set.
  if (op->disconnect_with_error != nullptr) {
    if (!grpc_error_get_int(op->disconnect_with_error,
                            GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE,
                            &value) ||
        static_cast<grpc_connectivity_state>(value) != GRPC_CHANNEL_IDLE) {
      // Disconnect.
      // Set the state to BUSY so the timer will not be set again.
      chand->IncreaseCallCount();
      if (chand->state_.Load(grpc_core::MemoryOrder::RELAXED) ==
          CHANNEL_STATE_BUSY_FROM_LEISURE) {
        grpc_timer_cancel(&chand->idle_timer_);
      }
    }
  }
  // Pass the op to the next filter.
  grpc_channel_next_op(elem, op);
}

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

grpc_error* ChannelData::Init(grpc_channel_element* elem,
                              grpc_channel_element_args* args) {
  grpc_error* error = GRPC_ERROR_NONE;
  new (elem->channel_data) ChannelData(args, &error);
  return error;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

}  // namespace

const grpc_channel_filter grpc_idle_filter = {
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
    "idle"};

static bool maybe_add_idle_filter(grpc_channel_stack_builder* builder,
                                  void* arg) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  bool enable =
      grpc_channel_arg_get_integer(
          grpc_channel_args_find(channel_args, GRPC_ARG_MAX_CONNECTION_IDLE_MS),
          {DEFAULT_MAX_LEISURE_TIME_MS, 0, INT_MAX}) != INT_MAX;
  if (enable) {
    GRPC_IDLE_FILTER_LOG("enabled");
    return grpc_channel_stack_builder_prepend_filter(builder, &grpc_idle_filter,
                                                     nullptr, nullptr);
  } else {
    GRPC_IDLE_FILTER_LOG("disabled");
    return true;
  }
}

void grpc_idle_filter_init(void) {
  GRPC_IDLE_FILTER_LOG("init");
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_idle_filter, nullptr);
}

void grpc_idle_filter_shutdown(void) {}