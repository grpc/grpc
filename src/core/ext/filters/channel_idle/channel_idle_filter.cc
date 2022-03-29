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

#include <limits.h>
#include <stdlib.h>

#include <atomic>
#include <limits>

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/capture.h"
#include "src/core/lib/gprpp/single_set_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/http2_errors.h"

// TODO(ctiller): The idle filter was disabled in client channel by default
// due to b/143502997. Now the bug is fixed enable the filter by default.
#define DEFAULT_IDLE_TIMEOUT Duration::Infinity()
// The user input idle timeout smaller than this would be capped to it.
#define MIN_IDLE_TIMEOUT_MS (1 /*second*/ * 1000)

// If these settings change, make sure that we are not sending a GOAWAY for
// inproc transport, since a GOAWAY to inproc ends up destroying the transport.
#define DEFAULT_MAX_CONNECTION_AGE Duration::Infinity()
#define DEFAULT_MAX_CONNECTION_AGE_GRACE Duration::Infinity()
#define DEFAULT_MAX_CONNECTION_IDLE Duration::Infinity()
#define MAX_CONNECTION_AGE_JITTER 0.1

namespace grpc_core {

TraceFlag grpc_trace_client_idle_filter(false, "client_idle_filter");

#define GRPC_IDLE_FILTER_LOG(format, ...)                               \
  do {                                                                  \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_client_idle_filter)) {       \
      gpr_log(GPR_INFO, "(client idle filter) " format, ##__VA_ARGS__); \
    }                                                                   \
  } while (0)

namespace {

using SingleSetActivityPtr =
    SingleSetPtr<Activity, typename ActivityPtr::deleter_type>;

Duration GetClientIdleTimeout(const ChannelArgs& args) {
  return args.GetDurationFromIntMillis(GRPC_ARG_CLIENT_IDLE_TIMEOUT_MS)
      .value_or(DEFAULT_IDLE_TIMEOUT);
}

struct MaxAgeConfig {
  Duration max_connection_age;
  Duration max_connection_idle;
  Duration max_connection_age_grace;

  bool enable() const {
    return max_connection_age != Duration::Infinity() ||
           max_connection_idle != Duration::Infinity();
  }
};

/* A random jitter of +/-10% will be added to MAX_CONNECTION_AGE to spread out
   connection storms. Note that the MAX_CONNECTION_AGE option without jitter
   would not create connection storms by itself, but if there happened to be a
   connection storm it could cause it to repeat at a fixed period. */
MaxAgeConfig GetMaxAgeConfig(ChannelArgs args) {
  const Duration args_max_age =
      args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_AGE)
          .value_or(DEFAULT_MAX_CONNECTION_AGE);
  const Duration args_max_idle =
      args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_IDLE)
          .value_or(DEFAULT_MAX_CONNECTION_IDLE);
  const Duration args_max_age_grace =
      args.GetDurationFromIntMillis(GRPC_ARG_MAX_CONNECTION_AGE_GRACE)
          .value_or(DEFAULT_MAX_CONNECTION_AGE_GRACE);
  /* generate a random number between 1 - MAX_CONNECTION_AGE_JITTER and
   1 + MAX_CONNECTION_AGE_JITTER */
  const double multiplier =
      rand() * MAX_CONNECTION_AGE_JITTER * 2.0 / RAND_MAX + 1.0 -
      MAX_CONNECTION_AGE_JITTER;
  /* GRPC_MILLIS_INF_FUTURE - 0.5 converts the value to float, so that result
     will not be cast to int implicitly before the comparison. */
  return MaxAgeConfig{args_max_age * multiplier, args_max_idle,
                      args_max_age_grace};
}

class ChannelIdleFilter : public ChannelFilter {
 public:
  ~ChannelIdleFilter() override = default;

  ChannelIdleFilter(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter& operator=(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter(ChannelIdleFilter&&) = default;
  ChannelIdleFilter& operator=(ChannelIdleFilter&&) = default;

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

  bool StartTransportOp(grpc_transport_op* op) override;

 protected:
  ChannelIdleFilter(grpc_channel_stack* channel_stack,
                    Duration client_idle_timeout)
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
  Duration client_idle_timeout_;
  std::shared_ptr<IdleFilterState> idle_filter_state_{
      std::make_shared<IdleFilterState>(false)};

  SingleSetActivityPtr activity_;
};

class ClientIdleFilter final : public ChannelIdleFilter {
 public:
  static absl::StatusOr<ClientIdleFilter> Create(
      ChannelArgs args, ChannelFilter::Args filter_args);

 private:
  using ChannelIdleFilter::ChannelIdleFilter;
};

class MaxAgeFilter final : public ChannelIdleFilter {
 public:
  static absl::StatusOr<MaxAgeFilter> Create(ChannelArgs args,
                                             ChannelFilter::Args filter_args);

  void Start();

 private:
  class ConnectivityWatcher : public AsyncConnectivityStateWatcherInterface {
   public:
    explicit ConnectivityWatcher(MaxAgeFilter* filter)
        : channel_stack_(filter->channel_stack()->Ref()), filter_(filter) {}
    ~ConnectivityWatcher() override = default;

    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   const absl::Status&) override {
      if (new_state == GRPC_CHANNEL_SHUTDOWN) filter_->Shutdown();
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

  SingleSetActivityPtr max_age_activity_;
  Duration max_connection_age_;
  Duration max_connection_age_grace_;
};

absl::StatusOr<ClientIdleFilter> ClientIdleFilter::Create(
    ChannelArgs args, ChannelFilter::Args filter_args) {
  ClientIdleFilter filter(filter_args.channel_stack(),
                          GetClientIdleTimeout(args));
  return absl::StatusOr<ClientIdleFilter>(std::move(filter));
}

absl::StatusOr<MaxAgeFilter> MaxAgeFilter::Create(
    ChannelArgs args, ChannelFilter::Args filter_args) {
  const auto config = GetMaxAgeConfig(args);
  MaxAgeFilter filter(filter_args.channel_stack(), config);
  return absl::StatusOr<MaxAgeFilter>(std::move(filter));
}

void MaxAgeFilter::Shutdown() {
  max_age_activity_.Reset();
  ChannelIdleFilter::Shutdown();
}

void MaxAgeFilter::Start() {
  // Trigger idle timer immediately
  IncreaseCallCount();
  DecreaseCallCount();

  struct StartupClosure {
    RefCountedPtr<grpc_channel_stack> channel_stack;
    MaxAgeFilter* filter;
    grpc_closure closure;
  };
  auto run_startup = [](void* p, grpc_error_handle) {
    auto* startup = static_cast<StartupClosure*>(p);
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
  ExecCtx::Run(DEBUG_LOCATION, &startup->closure, GRPC_ERROR_NONE);

  auto channel_stack = this->channel_stack()->Ref();

  // Start the max age timer
  if (max_connection_age_ != Duration::Infinity()) {
    max_age_activity_.Set(MakeActivity(
        TrySeq(
            // First sleep until the max connection age
            Sleep(ExecCtx::Get()->Now() + max_connection_age_),
            // Then send a goaway.
            [this] {
              GRPC_CHANNEL_STACK_REF(this->channel_stack(),
                                     "max_age send_goaway");
              // Jump out of the activity to send the goaway.
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
            // Sleep for the grace period
            [this] {
              return Sleep(ExecCtx::Get()->Now() + max_connection_age_grace_);
            }),
        ExecCtxWakeupScheduler(), [channel_stack, this](absl::Status status) {
          // OnDone -- close the connection if the promise completed
          // successfully.
          // (if it did not, it was cancelled)
          if (status.ok()) CloseChannel();
        }));
  }
}

// Construct a promise for one call.
ArenaPromise<ServerMetadataHandle> ChannelIdleFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  using Decrementer = std::unique_ptr<ChannelIdleFilter, CallCountDecreaser>;
  IncreaseCallCount();
  return ArenaPromise<ServerMetadataHandle>(
      Capture([](Decrementer*, ArenaPromise<ServerMetadataHandle>* next)
                  -> Poll<ServerMetadataHandle> { return (*next)(); },
              Decrementer(this), next_promise_factory(std::move(call_args))));
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
  activity_.Reset();
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
  activity_.Set(MakeActivity(std::move(promise), ExecCtxWakeupScheduler{},
                             [channel_stack, this](absl::Status status) {
                               if (status.ok()) CloseChannel();
                             }));
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
            GetClientIdleTimeout(channel_args) != Duration::Infinity()) {
          builder->PrependFilter(&grpc_client_idle_filter, nullptr);
        }
        return true;
      });
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        const grpc_channel_args* channel_args = builder->channel_args();
        if (!grpc_channel_args_want_minimal_stack(channel_args) &&
            GetMaxAgeConfig(channel_args).enable()) {
          builder->PrependFilter(
              &grpc_max_age_filter,
              [](grpc_channel_stack*, grpc_channel_element* elem) {
                static_cast<MaxAgeFilter*>(elem->channel_data)->Start();
              });
        }
        return true;
      });
}
}  // namespace grpc_core
