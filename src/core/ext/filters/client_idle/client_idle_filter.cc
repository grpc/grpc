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
#include "src/core/lib/gprpp/capture.h"
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

class ClientIdleFilter {
 public:
  static absl::StatusOr<ClientIdleFilter> Create(
      const grpc_channel_args* args, grpc_channel_stack* channel_stack);

  ClientIdleFilter(const ClientIdleFilter&) = delete;
  ClientIdleFilter& operator=(const ClientIdleFilter&) = delete;
  ClientIdleFilter(ClientIdleFilter&&) = default;
  ClientIdleFilter& operator=(ClientIdleFilter&&) = default;

  // Construct a promise for one call.
  ArenaPromise<TrailingMetadata> MakeCallPromise(
      ClientInitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory);

 private:
  ClientIdleFilter(grpc_channel_stack* channel_stack,
                   grpc_millis client_idle_timeout)
      : channel_stack_(channel_stack),
        client_idle_timeout_(client_idle_timeout) {}
  ~ClientIdleFilter() = default;

  void StartTransportOp(grpc_channel_element* elem, grpc_transport_op* op);

  void StartIdleTimer();

  void EnterIdle();

  void IncreaseCallCount();
  void DecreaseCallCount();

  struct CallCountDecreaser {
    void operator()(ClientIdleFilter* filter) const {
      filter->DecreaseCallCount();
    }
  };

  // The channel stack to which we take refs for pending callbacks.
  grpc_channel_stack* channel_stack_;
  grpc_millis client_idle_timeout_;
  std::shared_ptr<IdleFilterState> idle_filter_state_{
      std::make_shared<IdleFilterState>(false)};

  ActivityPtr activity_;
};

absl::StatusOr<ClientIdleFilter> ClientIdleFilter::Create(
    const grpc_channel_args* args, grpc_channel_stack* channel_stack) {
  ClientIdleFilter filter(channel_stack, GetClientIdleTimeout(args));
  return absl::StatusOr<ClientIdleFilter>(std::move(filter));
}

// Construct a promise for one call.
ArenaPromise<TrailingMetadata> ClientIdleFilter::MakeCallPromise(
    ClientInitialMetadata initial_metadata,
    NextPromiseFactory next_promise_factory) {
  using Decrementer = std::unique_ptr<ClientIdleFilter, CallCountDecreaser>;
  IncreaseCallCount();
  return ArenaPromise<TrailingMetadata>(Capture(
      [](Decrementer*, ArenaPromise<TrailingMetadata*>* next) {
        return (*next)();
      },
      Decrementer(this), next_promise_factory(std::move(initial_metadata))));
}

void ClientIdleFilter::StartTransportOp(grpc_channel_element* elem,
                                        grpc_transport_op* op) {
  // Catch the disconnect_with_error transport op.
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    // IncreaseCallCount() introduces a phony call and prevent the timer from
    // being reset by other threads.
    IncreaseCallCount();
    activity_.reset();
  }
  // Pass the op to the next filter.
  grpc_channel_next_op(elem, op);
}

void ClientIdleFilter::IncreaseCallCount() {
  idle_filter_state_->IncreaseCallCount();
}

void ClientIdleFilter::DecreaseCallCount() {
  if (idle_filter_state_->DecreaseCallCount()) {
    // If there are no more calls in progress, start the idle timer.
    StartIdleTimer();
  }
}

void ChannelData::StartIdleTimer() {
  GRPC_IDLE_FILTER_LOG("timer has started");
  auto idle_filter_state = idle_filter_state_;
  // Hold a ref to the channel stack for the timer callback.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "max idle timer callback");
  // DO NOT SUBMIT: activity should hold channel stack..
  activity_ = MakeActivity(Loop(TrySeq(
      Sleep(client_idle_timeout_),
      [idle_filter_state]() -> LoopCtl<absl::Status> {
        if (idle_filter_state->CheckTimer()) {
          return Continue{};
        } else {
          return []() {
            auto* op = grpc_transport_op_create();
            op->disconnect_with_error = grpc_error_set_int(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING("enter idle"),
                GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, GRPC_CHANNEL_IDLE);
            // Pass the transport op down to the channel stack.
            grpc_channel_next_op(elem_, op);
            return absl::OkStatus();
          };
        }
      })));
}

void ChannelData::EnterIdle() {
  GRPC_IDLE_FILTER_LOG("the channel will enter IDLE");
  // Hold a ref to the channel stack for the transport op.
  GRPC_CHANNEL_STACK_REF(channel_stack_, "idle transport op");
  // Pass the transport op down to the channel stack.
  grpc_channel_next_op(elem_, &idle_transport_op_);
}

const grpc_channel_filter grpc_client_idle_filter =
    MakePromiseBasedFilter<ClientIdleFilter, FilterEndpoint::kClient>(
        "client_idle");

}  // namespace

void RegisterClientIdleFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        const grpc_channel_args* channel_args = builder->channel_args();
        if (!grpc_channel_args_want_minimal_stack(channel_args) &&
            GetClientIdleTimeout(channel_args) != INT_MAX) {
          builder->PrependFilter(&grpc_client_idle_filter, nullptr);
        }
        return true;
      });
}
}  // namespace grpc_core
