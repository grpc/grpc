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
#include <stdlib.h>

#include <atomic>

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/capture.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/http2_errors.h"

// TODO(juanlishen): The idle filter is disabled in client channel by default
// due to b/143502997. Try to fix the bug and enable the filter by default.
#define DEFAULT_IDLE_TIMEOUT_MS INT_MAX
// The user input idle timeout smaller than this would be capped to it.
#define MIN_IDLE_TIMEOUT_MS (1 /*second*/ * 1000)

/* If these settings change, make sure that we are not sending a GOAWAY for
 * inproc transport, since a GOAWAY to inproc ends up destroying the transport.
 */
#define DEFAULT_MAX_CONNECTION_AGE_MS INT_MAX
#define DEFAULT_MAX_CONNECTION_AGE_GRACE_MS INT_MAX
#define DEFAULT_MAX_CONNECTION_IDLE_MS INT_MAX
#define MAX_CONNECTION_AGE_JITTER 0.1

#define MAX_CONNECTION_AGE_INTEGER_OPTIONS \
  { DEFAULT_MAX_CONNECTION_AGE_MS, 1, INT_MAX }
#define MAX_CONNECTION_IDLE_INTEGER_OPTIONS \
  { DEFAULT_MAX_CONNECTION_IDLE_MS, 1, INT_MAX }

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

struct MaxAgeConfig {
  grpc_millis max_connection_age;
  grpc_millis max_connection_idle;
  grpc_millis max_connection_age_grace;

  bool enable() const {
    return max_connection_age != GRPC_MILLIS_INF_FUTURE ||
           max_connection_idle != GRPC_MILLIS_INF_FUTURE;
  }
};

/* A random jitter of +/-10% will be added to MAX_CONNECTION_AGE to spread out
   connection storms. Note that the MAX_CONNECTION_AGE option without jitter
   would not create connection storms by itself, but if there happened to be a
   connection storm it could cause it to repeat at a fixed period. */
MaxAgeConfig GetMaxAgeConfig(const grpc_channel_args* args) {
  const int args_max_age = grpc_channel_arg_get_integer(
      grpc_channel_args_find(args, GRPC_ARG_MAX_CONNECTION_AGE_MS),
      MAX_CONNECTION_AGE_INTEGER_OPTIONS);
  const int args_max_idle = grpc_channel_arg_get_integer(
      grpc_channel_args_find(args, GRPC_ARG_MAX_CONNECTION_IDLE_MS),
      MAX_CONNECTION_IDLE_INTEGER_OPTIONS);
  const int args_max_age_grace = grpc_channel_arg_get_integer(
      grpc_channel_args_find(args, GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS),
      {DEFAULT_MAX_CONNECTION_AGE_GRACE_MS, 0, INT_MAX});
  /* generate a random number between 1 - MAX_CONNECTION_AGE_JITTER and
   1 + MAX_CONNECTION_AGE_JITTER */
  const double multiplier =
      rand() * MAX_CONNECTION_AGE_JITTER * 2.0 / RAND_MAX + 1.0 -
      MAX_CONNECTION_AGE_JITTER;
  const double max_age = multiplier * args_max_age;
  /* GRPC_MILLIS_INF_FUTURE - 0.5 converts the value to float, so that result
     will not be cast to int implicitly before the comparison. */
  return MaxAgeConfig{
      args_max_age == INT_MAX ||
              max_age > (static_cast<double>(GRPC_MILLIS_INF_FUTURE)) - 0.5
          ? GRPC_MILLIS_INF_FUTURE
          : static_cast<grpc_millis>(args_max_age),
      args_max_idle == INT_MAX ? GRPC_MILLIS_INF_FUTURE : args_max_idle,
      args_max_age_grace == INT_MAX ? GRPC_MILLIS_INF_FUTURE
                                    : args_max_age_grace};
}

class ChannelIdleFilter : public ChannelFilter {
 public:
  ~ChannelIdleFilter() override = default;

  ChannelIdleFilter(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter& operator=(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter(ChannelIdleFilter&&) = default;
  ChannelIdleFilter& operator=(ChannelIdleFilter&&) = default;

  // Construct a promise for one call.
  ArenaPromise<TrailingMetadata> MakeCallPromise(
      ClientInitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory) override;

  bool StartTransportOp(grpc_transport_op* op) override;

 protected:
  ChannelIdleFilter(grpc_channel_stack* channel_stack,
                    grpc_millis client_idle_timeout)
      : channel_stack_(channel_stack),
        client_idle_timeout_(client_idle_timeout) {}

  grpc_channel_stack* channel_stack() { return channel_stack_; };

  virtual void Shutdown();
  void CloseChannel();

  void IncreaseCallCount();
  void DecreaseCallCount();

 private:
  void StartIdleTimer();

  struct CallCountDecreaser {
    void operator()(ChannelIdleFilter* filter) const {
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

class ClientIdleFilter final : public ChannelIdleFilter {
 public:
  static absl::StatusOr<ClientIdleFilter> Create(
      const grpc_channel_args* args, ChannelFilter::Args filter_args);

 private:
  using ChannelIdleFilter::ChannelIdleFilter;
};

class MaxAgeFilter final : public ChannelIdleFilter {
 public:
  static absl::StatusOr<MaxAgeFilter> Create(const grpc_channel_args* args,
                                             ChannelFilter::Args filter_args);

  void Start() override;

 private:
  class ConnectivityWatcher : public AsyncConnectivityStateWatcherInterface {
   public:
    explicit ConnectivityWatcher(MaxAgeFilter* filter)
        : channel_stack_(filter->channel_stack()->Ref()), filter_(filter) {}
    ~ConnectivityWatcher() override = default;

    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   const absl::Status&) override {
      if (new_state == GRPC_CHANNEL_SHUTDOWN) {
        filter_->Shutdown();
      }
    }

   private:
    RefCountedPtr<grpc_channel_stack> channel_stack_;
    MaxAgeFilter* filter_;
  };

  MaxAgeFilter(grpc_channel_stack* channel_stack,
               const MaxAgeConfig& max_age_config)
      : ChannelIdleFilter(channel_stack, max_age_config.max_connection_idle),
        max_connection_age_(max_age_config.max_connection_age),
        max_connection_age_grace_(max_age_config.max_connection_age_grace) {}

  void Shutdown() override;

  ActivityPtr max_age_activity_;
  grpc_millis max_connection_age_;
  grpc_millis max_connection_age_grace_;
};

absl::StatusOr<ClientIdleFilter> ClientIdleFilter::Create(
    const grpc_channel_args* args, ChannelFilter::Args filter_args) {
  ClientIdleFilter filter(filter_args.channel_stack(),
                          GetClientIdleTimeout(args));
  return absl::StatusOr<ClientIdleFilter>(std::move(filter));
}

absl::StatusOr<MaxAgeFilter> MaxAgeFilter::Create(
    const grpc_channel_args* args, ChannelFilter::Args filter_args) {
  const auto config = GetMaxAgeConfig(args);
  MaxAgeFilter filter(filter_args.channel_stack(), config);
  return absl::StatusOr<MaxAgeFilter>(std::move(filter));
}

void MaxAgeFilter::Shutdown() {
  max_age_activity_.reset();
  ChannelIdleFilter::Shutdown();
}

void MaxAgeFilter::Start() {
  // Trigger idle timer immediately
  IncreaseCallCount();
  DecreaseCallCount();

  auto channel_stack = this->channel_stack()->Ref();

  // Start the idle timer
  max_age_activity_ = MakeActivity(
      TrySeq(
          Sleep(ExecCtx::Get()->Now() + max_connection_age_),
          [this] {
            GRPC_CHANNEL_STACK_REF(this->channel_stack(),
                                   "max_age send_goaway");
            // Jump out of the activity to sent the goaway.
            auto fn = [](void* arg, grpc_error_handle) {
              auto* channel_stack = static_cast<grpc_channel_stack*>(arg);
              grpc_transport_op* op = grpc_make_transport_op(nullptr);
              op->goaway_error = grpc_error_set_int(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("max_age"),
                  GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_NO_ERROR);
              grpc_channel_element* elem =
                  grpc_channel_stack_element(channel_stack, 0);
              elem->filter->start_transport_op(elem, op);
              GRPC_CHANNEL_STACK_UNREF(channel_stack, "max_age send_goaway");
            };
            ExecCtx::Run(
                DEBUG_LOCATION,
                GRPC_CLOSURE_CREATE(fn, this->channel_stack(), nullptr),
                GRPC_ERROR_NONE);
            return Immediate(absl::OkStatus());
          },
          [this] {
            return Sleep(ExecCtx::Get()->Now() + max_connection_age_grace_);
          }),
      ExecCtxWakeupScheduler(), [channel_stack, this](absl::Status status) {
        if (status.ok()) CloseChannel();
      });
}

// Construct a promise for one call.
ArenaPromise<TrailingMetadata> ChannelIdleFilter::MakeCallPromise(
    ClientInitialMetadata initial_metadata,
    NextPromiseFactory next_promise_factory) {
  using Decrementer = std::unique_ptr<ChannelIdleFilter, CallCountDecreaser>;
  IncreaseCallCount();
  return ArenaPromise<TrailingMetadata>(Capture(
      [](Decrementer*, ArenaPromise<TrailingMetadata>* next)
          -> Poll<TrailingMetadata> { return (*next)(); },
      Decrementer(this), next_promise_factory(std::move(initial_metadata))));
}

bool ChannelIdleFilter::StartTransportOp(grpc_transport_op* op) {
  // Catch the disconnect_with_error transport op.
  if (op->disconnect_with_error != GRPC_ERROR_NONE) Shutdown();
  // Pass the op to the next filter.
  return false;
}

void ChannelIdleFilter::Shutdown() {
  // IncreaseCallCount() introduces a phony call and prevent the timer from
  // being reset by other threads.
  IncreaseCallCount();
  activity_.reset();
}

void ChannelIdleFilter::IncreaseCallCount() {
  idle_filter_state_->IncreaseCallCount();
}

void ChannelIdleFilter::DecreaseCallCount() {
  if (idle_filter_state_->DecreaseCallCount()) {
    // If there are no more calls in progress, start the idle timer.
    StartIdleTimer();
  }
}

void ChannelIdleFilter::StartIdleTimer() {
  GRPC_IDLE_FILTER_LOG("timer has started");
  auto idle_filter_state = idle_filter_state_;
  // Hold a ref to the channel stack for the timer callback.
  auto channel_stack = channel_stack_->Ref();
  auto timeout = client_idle_timeout_;
  auto promise = Loop([timeout, idle_filter_state]() {
    return TrySeq(Sleep(ExecCtx::Get()->Now() + timeout),
                  [idle_filter_state]() -> Poll<LoopCtl<absl::Status>> {
                    if (idle_filter_state->CheckTimer()) {
                      return Continue{};
                    } else {
                      return absl::OkStatus();
                    }
                  });
  });
  activity_ = MakeActivity(std::move(promise), ExecCtxWakeupScheduler{},
                           [channel_stack, this](absl::Status status) {
                             if (status.ok()) CloseChannel();
                           });
}

void ChannelIdleFilter::CloseChannel() {
  auto* op = grpc_make_transport_op(nullptr);
  op->disconnect_with_error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("enter idle"),
      GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, GRPC_CHANNEL_IDLE);
  // Pass the transport op down to the channel stack.
  auto* elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

const grpc_channel_filter grpc_client_idle_filter =
    MakePromiseBasedFilter<ClientIdleFilter, FilterEndpoint::kClient>(
        "client_idle");
const grpc_channel_filter grpc_max_age_filter =
    MakePromiseBasedFilter<MaxAgeFilter, FilterEndpoint::kServer>("max_age");

}  // namespace

void RegisterChannelIdleFilters(CoreConfiguration::Builder* builder) {
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
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        const grpc_channel_args* channel_args = builder->channel_args();
        if (GetMaxAgeConfig(channel_args).enable()) {
          builder->PrependFilter(&grpc_max_age_filter, nullptr);
        }
        return true;
      });
}
}  // namespace grpc_core
