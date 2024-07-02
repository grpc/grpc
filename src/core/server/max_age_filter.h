// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_SERVER_MAX_AGE_FILTER_H
#define GRPC_SRC_CORE_SERVER_MAX_AGE_FILTER_H

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/server/server_interface.h"

namespace grpc_core {

class MaxAgeFilter final : public DualRefCounted<MaxAgeFilter> {
 public:
  MaxAgeFilter(ConnectionId connection_id, ServerInterface* server);

  void Orphaned() override;

  class Call {
   public:
    explicit Call(MaxAgeFilter* filter) : filter_(filter) {
      filter_->IncreaseCallCount();
    }
    ~Call() { filter_->DecreaseCallCount(); }
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;

   private:
    MaxAgeFilter* const filter_;
  };

 private:
  void IncreaseCallCount() { idle_state_.IncreaseCallCount(); }
  void DecreaseCallCount() {
    if (idle_state_.DecreaseCallCount()) {
      StartTimer();
    }
  }
  void StartTimer();
  void FinishTimer();

  IdleFilterState idle_state_;
  const ConnectionId connection_id_;
  StepTimer::Handle max_age_timer_{0, 0};
  ServerInterface* const server_;
};

}  // namespace grpc_core

#endif
