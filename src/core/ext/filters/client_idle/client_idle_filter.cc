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

#include <atomic>

#include "src/core/ext/filters/client_idle/idle_filter_state.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/http2_errors.h"

// TODO(juanlishen): The idle filter is disabled in client channel by default
// due to b/143502997. Try to fix the bug and enable the filter by default.
#define DEFAULT_IDLE_TIMEOUT_MS INT_MAX
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

grpc_millis GetClientIdleTimeout(const grpc_channel_args* args) {
  return std::max(
      grpc_channel_arg_get_integer(
          grpc_channel_args_find(args, GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS),
          {DEFAULT_IDLE_TIMEOUT_MS, 0, INT_MAX}),
      MIN_IDLE_TIMEOUT_MS);
}

class ChannelData {
 public:
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op);

  void IncreaseCallCount();

  void DecreaseCallCount();

 private:
  ChannelData(grpc_channel_element* elem, grpc_channel_element_args* args,
              grpc_error_handle* error);
  ~ChannelData() = default;

  static void IdleTimerCallback(void* arg, grpc_error_handle error);
  static void IdleTransportOpCompleteCallback(void* arg,
                                              grpc_error_handle error);

  void StartIdleTimer();

  void EnterIdle();

  grpc_channel_element* elem_;
  // The channel stack to which we take refs for pending callbacks.
  grpc_channel_stack* channel_stack_;
  // Timeout after the last RPC finishes on the client channel at which the
  // channel goes back into IDLE state.
  const grpc_millis client_idle_timeout_;

  // Member data used to track the state of channel.
  IdleFilterState idle_filter_state_{false};

  // Idle timer and its callback closure.
  grpc_timer idle_timer_;
  grpc_closure idle_timer_callback_;

  // The transport op telling the client channel to enter IDLE.
  grpc_transport_op idle_transport_op_;
  grpc_closure idle_transport_op_complete_callback_;
};

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  grpc_error_handle error = GRPC_ERROR_NONE;
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
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    // IncreaseCallCount() introduces a phony call and prevent the timer from
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
  idle_filter_state_.IncreaseCallCount();
}

void ChannelData::DecreaseCallCount() {
  if (idle_filter_state_.DecreaseCallCount()) {
    // If there are no more calls in progress, start the idle timer.
    StartIdleTimer();
  }
}

ChannelData::ChannelData(grpc_channel_element* elem,
                         grpc_channel_element_args* args,
                         grpc_error_handle* /*error*/)
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

void ChannelData::IdleTimerCallback(void* arg, grpc_error_handle error) {
  GRPC_IDLE_FILTER_LOG("timer alarms");
  ChannelData* chand = static_cast<ChannelData*>(arg);
  if (error != GRPC_ERROR_NONE) {
    GRPC_IDLE_FILTER_LOG("timer canceled");
    GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
    return;
  }
  if (chand->idle_filter_state_.CheckTimer()) {
    chand->StartIdleTimer();
  } else {
    chand->EnterIdle();
  }
  GRPC_IDLE_FILTER_LOG("timer finishes");
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "max idle timer callback");
}

void ChannelData::IdleTransportOpCompleteCallback(void* arg,
                                                  grpc_error_handle /*error*/) {
  ChannelData* chand = static_cast<ChannelData*>(arg);
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack_, "idle transport op");
}

void ChannelData::StartIdleTimer() {
  GRPC_IDLE_FILTER_LOG("timer has started");
  // Hold a ref to the channel stack for the timer callback.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "max idle timer callback");
  grpc_timer_init(&idle_timer_, ExecCtx::Get()->Now() + client_idle_timeout_,
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
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
};

grpc_error_handle CallData::Init(grpc_call_element* elem,
                                 const grpc_call_element_args* /*args*/) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->IncreaseCallCount();
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*ignored*/) {
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

}  // namespace

void RegisterClientIdleFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](grpc_channel_stack_builder* builder) {
        const grpc_channel_args* channel_args =
            grpc_channel_stack_builder_get_channel_arguments(builder);
        if (!grpc_channel_args_want_minimal_stack(channel_args) &&
            GetClientIdleTimeout(channel_args) != INT_MAX) {
          return grpc_channel_stack_builder_prepend_filter(
              builder, &grpc_client_idle_filter, nullptr, nullptr);
        } else {
          return true;
        }
      });
}
}  // namespace grpc_core
