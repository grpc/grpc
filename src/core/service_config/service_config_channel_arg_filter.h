//
// Copyright 2020 gRPC authors.
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
//

// This filter reads GRPC_ARG_SERVICE_CONFIG and populates ServiceConfigCallData
// in the call context per call for direct channels.

#ifndef GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CHANNEL_ARG_FILTER_H
#define GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CHANNEL_ARG_FILTER_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <memory>

#include "absl/status/statusor.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

class ServiceConfigChannelArgFilter final
    : public ImplementChannelFilter<ServiceConfigChannelArgFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() { return "service_config_channel_arg"; }

  static absl::StatusOr<std::unique_ptr<ServiceConfigChannelArgFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args) {
    return std::make_unique<ServiceConfigChannelArgFilter>(args);
  }

  explicit ServiceConfigChannelArgFilter(const ChannelArgs& args) {
    auto service_config_str = args.GetOwnedString(GRPC_ARG_SERVICE_CONFIG);
    if (service_config_str.has_value()) {
      auto service_config =
          ServiceConfigImpl::Create(args, *service_config_str);
      if (!service_config.ok()) {
        LOG(ERROR) << service_config.status().ToString();
      } else {
        service_config_ = std::move(*service_config);
      }
    }
  }

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md,
                                 ServiceConfigChannelArgFilter* filter);
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
  };

 private:
  RefCountedPtr<ServiceConfig> service_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVICE_CONFIG_SERVICE_CONFIG_CHANNEL_ARG_FILTER_H
