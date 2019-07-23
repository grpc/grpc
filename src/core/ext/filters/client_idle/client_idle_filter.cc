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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/http2_errors.h"

// The idle filter is disabled in client channel by default.
// To enable the idle filte, set GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS to [0, INT_MAX)
// in channel args.
// TODO(qianchengz): Find a reasonable default value. Maybe check what deault
// value Java uses.
#define DEFAULT_IDLE_TIMEOUT_MS (5 * 60 * 1000)

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
  ~ChannelData();

  class ClosureSetter;
  static void OnIncreaseCallCountToOneLocked(ChannelData* chand);
  static void OnDecreaseCallCountToZeroLocked(ChannelData* chand);
  static void ShutdownLocked(ChannelData* chand);
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

  // Combiner used to synchronize operations.
  grpc_combiner* combiner_;

  // Member data used to track the state of channel.
  bool shutdown_ = false;
  Atomic<size_t> call_count_;
  size_t inc_closure_cnt_ = 0;

  // Idle timer and its callback closure.
  bool timer_on_ = false;
  grpc_millis last_idle_time_;
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
    New<ClosureSetter>(ShutdownLocked, chand);
  }
  // Pass the op to the next filter.
  grpc_channel_next_op(elem, op);
}

void ChannelData::IncreaseCallCount() {
  size_t previous_value = call_count_.FetchAdd(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has increased to %" PRIuPTR,
                       previous_value + 1);
  if (previous_value == 0) {
    New<ClosureSetter>(OnIncreaseCallCountToOneLocked, this);
  }
}

void ChannelData::DecreaseCallCount() {
  size_t previous_value = call_count_.FetchSub(1, MemoryOrder::RELAXED);
  GRPC_IDLE_FILTER_LOG("call counter has decreased to %" PRIuPTR,
                       previous_value - 1);
  if (previous_value == 1) {
    New<ClosureSetter>(OnDecreaseCallCountToZeroLocked, this);
  }
}

ChannelData::ChannelData(grpc_channel_element* elem,
                         grpc_channel_element_args* args, grpc_error** error)
    : elem_(elem),
      channel_stack_(args->channel_stack),
      client_idle_timeout_(GetClientIdleTimeout(args->channel_args)),
      combiner_(grpc_combiner_create()),
      call_count_(0) {
  // If the idle filter is explicitly disabled in channel args, this ctor should
  // not get called.
  GPR_ASSERT(client_idle_timeout_ != GRPC_MILLIS_INF_FUTURE);
  GRPC_IDLE_FILTER_LOG("created with max_leisure_time = %" PRId64 " ms",
                       client_idle_timeout_);
  // Initialize the idle timer without setting it.
  grpc_timer_init_unset(&idle_timer_);
  // Initialize the idle timer callback closure.
  GRPC_CLOSURE_INIT(&idle_timer_callback_, IdleTimerCallback, this,
                    grpc_combiner_scheduler(combiner_));
  // Initialize the idle transport op complete callback.
  GRPC_CLOSURE_INIT(&idle_transport_op_complete_callback_,
                    IdleTransportOpCompleteCallback, this,
                    grpc_combiner_scheduler(combiner_));
}

ChannelData::~ChannelData() {
  GRPC_COMBINER_UNREF(combiner_, "client idle filter");
}

class ChannelData::ClosureSetter {
 public:
  typedef void (*Callback)(ChannelData* chand);
  ClosureSetter(Callback cb, ChannelData* chand) : cb_(cb), chand_(chand) {
    GRPC_CHANNEL_STACK_REF(chand_->channel_stack_,
                           "client idle filter callback");
    GRPC_CLOSURE_INIT(&closure_, CallbackWrapper, this,
                      grpc_combiner_scheduler(chand_->combiner_));
    GRPC_CLOSURE_SCHED(&closure_, GRPC_ERROR_NONE);
  }

 private:
  static void CallbackWrapper(void* arg, grpc_error* error) {
    ClosureSetter* self = static_cast<ClosureSetter*>(arg);
    self->cb_(self->chand_);
    GRPC_CHANNEL_STACK_UNREF(self->chand_->channel_stack_,
                             "client idle filter callback");
    Delete(self);
  }

  grpc_closure closure_;
  const Callback cb_;
  ChannelData* const chand_;
};

void ChannelData::OnIncreaseCallCountToOneLocked(ChannelData* chand) {
  chand->inc_closure_cnt_++;
}

void ChannelData::OnDecreaseCallCountToZeroLocked(ChannelData* chand) {
  chand->inc_closure_cnt_--;
  if (chand->shutdown_) return;
  if (chand->inc_closure_cnt_ == 0) {
    chand->last_idle_time_ = ExecCtx::Get()->Now();
    if (!chand->timer_on_) {
      chand->StartIdleTimer();
    }
  }
}

void ChannelData::ShutdownLocked(ChannelData* chand) {
  chand->shutdown_ = true;
  if (chand->timer_on_) grpc_timer_cancel(&chand->idle_timer_);
}

void ChannelData::IdleTimerCallback(void* arg, grpc_error* error) {
  GRPC_IDLE_FILTER_LOG("timer alarms");
  ChannelData* chand = static_cast<ChannelData*>(arg);
  chand->timer_on_ = false;
  if (error == GRPC_ERROR_NONE && !chand->shutdown_ && chand->inc_closure_cnt_ == 0) {
    if (ExecCtx::Get()->Now() >= chand->last_idle_time_ + chand->client_idle_timeout_) {
      chand->EnterIdle();
    } else {
      chand->StartIdleTimer();
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
  grpc_timer_init(&idle_timer_, ExecCtx::Get()->Now() + client_idle_timeout_,
                  &idle_timer_callback_);
}

void ChannelData::EnterIdle() {
  GRPC_IDLE_FILTER_LOG("the channel will enter IDLE");
  // Hold a ref to the channel stack for the transport op.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "idle transport op");
  // Initialize the transport op.
  memset(&idle_transport_op_, 0, sizeof(idle_transport_op_));
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
