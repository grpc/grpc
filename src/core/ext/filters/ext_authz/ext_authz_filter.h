//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/ext/filters/ext_authz/ext_authz_service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/status/statusor.h"

namespace grpc_core {

class ExtAuthzFilter : public ImplementChannelFilter<ExtAuthzFilter> {
 public:
  struct Config : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_authz_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    std::string instance_name;
    ExtAuthz ext_authz;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "ext_authz_filter"; }

  static absl::StatusOr<std::unique_ptr<ExtAuthzFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  class ChannelCache : public Blackboard::Entry {
   public:
    static UniqueTypeName Type();

    // RefCountedPtr<Channel> GetChannel(const ExtAuthz::GrpcService& service);

   private:
    Mutex mu_;
    std::map<std::string, RefCountedPtr<Channel>> channels_
        ABSL_GUARDED_BY(mu_);
  };

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md,
                                         ExtAuthzFilter* filter);
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

 private:
  explicit ExtAuthzFilter(const ExtAuthzParsedConfig::Config* filter_config,
                          RefCountedPtr<ExtAuthzClient> ext_authz_client)
      : filter_config_(filter_config),
        ext_authz_client_(std::move(ext_authz_client)) {}

  const ExtAuthzParsedConfig::Config* filter_config_;
  RefCountedPtr<ExtAuthzClient> ext_authz_client_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H
