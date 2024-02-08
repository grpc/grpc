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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CHANNEL_IDLE_CHANNEL_IDLE_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_CHANNEL_IDLE_CHANNEL_IDLE_FILTER_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/impl/connectivity_state.h>

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/single_set_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class ChannelIdleFilter : public ImplementChannelFilter<ChannelIdleFilter> {
 public:
  ~ChannelIdleFilter() override = default;

  ChannelIdleFilter(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter& operator=(const ChannelIdleFilter&) = delete;
  ChannelIdleFilter(ChannelIdleFilter&&) = default;
  ChannelIdleFilter& operator=(ChannelIdleFilter&&) = default;

  class Call {
   public:
    explicit Call(ChannelIdleFilter* filter) : filter_(filter) {
      filter_->IncreaseCallCount();
    }
    ~Call() { filter_->DecreaseCallCount(); }

    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;

   private:
    ChannelIdleFilter* filter_;
  };

  bool StartTransportOp(grpc_transport_op* op) override;

 protected:
  using SingleSetActivityPtr =
      SingleSetPtr<Activity, typename ActivityPtr::deleter_type>;

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

  // The channel stack to which we take refs for pending callbacks.
  grpc_channel_stack* channel_stack_;
  Duration client_idle_timeout_;
  std::shared_ptr<IdleFilterState> idle_filter_state_{
      std::make_shared<IdleFilterState>(false)};

  SingleSetActivityPtr activity_;
};

class ClientIdleFilter final : public ChannelIdleFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientIdleFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

 private:
  using ChannelIdleFilter::ChannelIdleFilter;
};

class MaxAgeFilter final : public ChannelIdleFilter {
 public:
  static const grpc_channel_filter kFilter;
  struct Config;

  static absl::StatusOr<MaxAgeFilter> Create(const ChannelArgs& args,
                                             ChannelFilter::Args filter_args);

  void PostInit() override;

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

  MaxAgeFilter(grpc_channel_stack* channel_stack, const Config& max_age_config);

  void Shutdown() override;

  SingleSetActivityPtr max_age_activity_;
  Duration max_connection_age_;
  Duration max_connection_age_grace_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CHANNEL_IDLE_CHANNEL_IDLE_FILTER_H
