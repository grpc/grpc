// Copyright 2022 gRPC authors.
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

// TODO(ctiller): Add a unit test suite for these filters once it's practical to
// mock transport operations.

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/channel_idle/channel_idle_filter.h"

#include <stdint.h>
#include <stdlib.h>

#include <functional>
#include <tuple>
#include <utility>

#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/http2_errors.h"

namespace grpc_core {

namespace {

// TODO(ctiller): The idle filter was disabled in client channel by default
// due to b/143502997. Now the bug is fixed enable the filter by default.
const auto kDefaultIdleTimeout = Duration::Infinity();

// If these settings change, make sure that we are not sending a GOAWAY for
// inproc transport, since a GOAWAY to inproc ends up destroying the transport.
const auto kDefaultMaxConnectionAge = Duration::Infinity();
const auto kDefaultMaxConnectionAgeGrace = Duration::Infinity();
const auto kDefaultMaxConnectionIdle = Duration::Infinity();
const auto kMaxConnectionAgeJitter = 0.1;

TraceFlag grpc_trace_client_idle_filter(false, "client_idle_filter");
}  // namespace

#define GRPC_IDLE_FILTER_LOG(format, ...)                               \
  do {                                                                  \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_client_idle_filter)) {       \
      gpr_log(GPR_INFO, "(client idle filter) " format, ##__VA_ARGS__); \
    }                                                                   \
  } while (0)

namespace {

Duration GetClientIdleTimeout(const ChannelArgs& args) {
  return args.GetDurationFromIntMillis(GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS)
      .value_or(kDefaultIdleTimeout);
}

}  // namespace

struct MaxAgeFilter::Config {
  Duration max_connection_age;
  Duration max_connection_idle;
  Duration max_connection_age_grace;

  bool enable() const {
    return max_connection_age != Duration::Infinity() ||
           max_connection_idle != Duration::Infinity();
  }

  // A random jitter of +/-10% will be added to MAX_CONNECTION_AGE to spread out
  // connection storms. Note that the MAX_CONNECTION_AGE option without jitter
  // would not create connection storms by itself, but if there happened to be a
  // connection storm it could cause it to repeat at a fixed period.
  static Config FromChannelArgs(const ChannelArgs& args) {
    const Duration args_max_age =
        args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_AGE_MS)
            .value_or(kDefaultMaxConnectionAge);
    const Duration args_max_idle =
        args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_IDLE_MS)
            .value_or(kDefaultMaxConnectionIdle);
    const Duration args_max_age_grace =
        args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS)
            .value_or(kDefaultMaxConnectionAgeGrace);
    // generate a random number between 1 - kMaxConnectionAgeJitter and
    // 1 + kMaxConnectionAgeJitter
    const double multiplier =
        rand() * kMaxConnectionAgeJitter * 2.0 / RAND_MAX + 1.0 -
        kMaxConnectionAgeJitter;
    // GRPC_MILLIS_INF_FUTURE - 0.5 converts the value to float, so that result
    // will not be cast to int implicitly before the comparison.
    return Config{args_max_age * multiplier, args_max_idle, args_max_age_grace};
  }
};

absl::StatusOr<ClientIdleFilter> ClientIdleFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  ClientIdleFilter filter(filter_args.channel_stack(),
                          GetClientIdleTimeout(args));
  return absl::StatusOr<ClientIdleFilter>(std::move(filter));
}

absl::StatusOr<MaxAgeFilter> MaxAgeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  MaxAgeFilter filter(filter_args.channel_stack(),
                      Config::FromChannelArgs(args));
  return absl::StatusOr<MaxAgeFilter>(std::move(filter));
}

void MaxAgeFilter::Shutdown() {
  max_age_activity_.Reset();
  ChannelIdleFilter::Shutdown();
}

void MaxAgeFilter::PostInit() {
  struct StartupClosure {
    RefCountedPtr<grpc_channel_stack> channel_stack;
    MaxAgeFilter* filter;
    grpc_closure closure;
  };
  auto run_startup = [](void* p, grpc_error_handle) {
    auto* startup = static_cast<StartupClosure*>(p);
    // Trigger idle timer
    GPR_ASSERT(startup->filter->IncreaseCallCount());
    startup->filter->DecreaseCallCount();
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->start_connectivity_watch.reset(
        new ConnectivityWatcher(startup->filter));
    op->start_connectivity_watch_state = GRPC_CHANNEL_IDLE;
    grpc_channel_next_op(
        grpc_channel_stack_element(startup->channel_stack.get(), 0), op);
    delete startup;
  };
  auto* startup =
      new StartupClosure{this->channel_stack()->Ref(), this, grpc_closure{}};
  GRPC_CLOSURE_INIT(&startup->closure, run_startup, startup, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &startup->closure, absl::OkStatus());

  auto channel_stack = this->channel_stack()->Ref();

  // Start the max age timer
  if (max_connection_age_ != Duration::Infinity()) {
    max_age_activity_.Set(MakeActivity(
        TrySeq(
            // First sleep until the max connection age
            Sleep(Timestamp::Now() + max_connection_age_),
            // Then send a goaway.
            [this] {
              GRPC_CHANNEL_STACK_REF(this->channel_stack(),
                                     "max_age send_goaway");
              // Jump out of the activity to send the goaway.
              auto fn = [](void* arg, grpc_error_handle) {
                auto* channel_stack = static_cast<grpc_channel_stack*>(arg);
                grpc_transport_op* op = grpc_make_transport_op(nullptr);
                op->goaway_error = grpc_error_set_int(
                    GRPC_ERROR_CREATE("max_age"),
                    StatusIntProperty::kHttp2Error, GRPC_HTTP2_NO_ERROR);
                grpc_channel_element* elem =
                    grpc_channel_stack_element(channel_stack, 0);
                elem->filter->start_transport_op(elem, op);
                GRPC_CHANNEL_STACK_UNREF(channel_stack, "max_age send_goaway");
              };
              ExecCtx::Run(
                  DEBUG_LOCATION,
                  GRPC_CLOSURE_CREATE(fn, this->channel_stack(), nullptr),
                  absl::OkStatus());
              return Immediate(absl::OkStatus());
            },
            // Sleep for the grace period
            [this] {
              return Sleep(Timestamp::Now() + max_connection_age_grace_);
            }),
        ExecCtxWakeupScheduler(),
        [channel_stack, this](absl::Status status) {
          // OnDone -- close the connection if the promise completed
          // successfully.
          // (if it did not, it was cancelled)
          if (status.ok()) CloseChannel();
        },
        channel_stack->EventEngine()));
  }
}

// Construct a promise for one call.
ArenaPromise<ServerMetadataHandle> ChannelIdleFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  using Decrementer = std::unique_ptr<ChannelIdleFilter, CallCountDecreaser>;
  if (!IncreaseCallCount()) {
    return Immediate(ServerMetadataFromStatus(
        absl::UnavailableError("connection closed due to idleness")));
  }
  return ArenaPromise<ServerMetadataHandle>(
      [decrementer = Decrementer(this),
       next = next_promise_factory(std::move(call_args))]() mutable
      -> Poll<ServerMetadataHandle> { return next(); });
}

bool ChannelIdleFilter::StartTransportOp(grpc_transport_op* op) {
  // Catch the disconnect_with_error transport op.
  if (!op->disconnect_with_error.ok()) Shutdown();
  // Pass the op to the next filter.
  return false;
}

void ChannelIdleFilter::Shutdown() {
  // IncreaseCallCount() introduces a phony call and prevent the timer from
  // being reset by other threads.
  std::ignore = IncreaseCallCount();
  activity_.Reset();
}

bool ChannelIdleFilter::IncreaseCallCount() {
  return idle_filter_state_->IncreaseCallCount();
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
    return TrySeq(Sleep(Timestamp::Now() + timeout),
                  [idle_filter_state]() -> Poll<LoopCtl<absl::Status>> {
                    if (idle_filter_state->CheckTimer()) {
                      return Continue{};
                    } else {
                      return absl::OkStatus();
                    }
                  });
  });
  activity_.Set(MakeActivity(
      std::move(promise), ExecCtxWakeupScheduler{},
      [channel_stack, this](absl::Status status) {
        if (status.ok()) CloseChannel();
      },
      channel_stack->EventEngine()));
}

void ChannelIdleFilter::CloseChannel() {
  class WaitForNonReady : public AsyncConnectivityStateWatcherInterface {
   public:
    explicit WaitForNonReady(ChannelIdleFilter* filter)
        : channel_stack_(filter->channel_stack()->Ref()), filter_(filter) {}
    ~WaitForNonReady() override = default;

    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   const absl::Status&) override {
      if (new_state == GRPC_CHANNEL_READY) return;
      if (filter_->idle_filter_state_->ResetTimerExpiry()) {
        filter_->StartIdleTimer();
      }
    }

   private:
    RefCountedPtr<grpc_channel_stack> channel_stack_;
    ChannelIdleFilter* filter_;
  };
  if (idle_filter_state_->StartNonIdleWatch()) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->start_connectivity_watch.reset(new WaitForNonReady(this));
    op->start_connectivity_watch_state = GRPC_CHANNEL_READY;
    grpc_channel_next_op(grpc_channel_stack_element(channel_stack(), 0), op);
  }

  auto* op = grpc_make_transport_op(nullptr);
  op->disconnect_with_error = grpc_error_set_int(
      GRPC_ERROR_CREATE("enter idle"),
      StatusIntProperty::ChannelConnectivityState, GRPC_CHANNEL_IDLE);
  // Pass the transport op down to the channel stack.
  auto* elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

const grpc_channel_filter ClientIdleFilter::kFilter =
    MakePromiseBasedFilter<ClientIdleFilter, FilterEndpoint::kClient>(
        "client_idle");
const grpc_channel_filter MaxAgeFilter::kFilter =
    MakePromiseBasedFilter<MaxAgeFilter, FilterEndpoint::kServer>("max_age");

void RegisterChannelIdleFilters(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        auto channel_args = builder->channel_args();
        if (!channel_args.WantMinimalStack() &&
            GetClientIdleTimeout(channel_args) != Duration::Infinity()) {
          builder->PrependFilter(&ClientIdleFilter::kFilter);
        }
        return true;
      });
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        auto channel_args = builder->channel_args();
        if (!channel_args.WantMinimalStack() &&
            MaxAgeFilter::Config::FromChannelArgs(channel_args).enable()) {
          builder->PrependFilter(&MaxAgeFilter::kFilter);
        }
        return true;
      });
}

MaxAgeFilter::MaxAgeFilter(grpc_channel_stack* channel_stack,
                           const Config& max_age_config)
    : ChannelIdleFilter(channel_stack, max_age_config.max_connection_idle),
      max_connection_age_(max_age_config.max_connection_age),
      max_connection_age_grace_(max_age_config.max_connection_age_grace) {}

}  // namespace grpc_core
