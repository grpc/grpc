//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/authorization/rbac_policy.h"
#include "src/core/lib/transport/transport.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// Filter used when xDS server config fetcher provides a configuration with an
// HTTP RBAC filter. Also serves as the type for channel data for the filter.
class RbacFilter : public ImplementChannelFilter<RbacFilter> {
 public:
  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("rbac_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      return rbac == DownCast<const Config&>(other).rbac;
    }
    std::string ToString() const override { return rbac.ToString(); }

    Rbac rbac;
  };

  // This channel filter is intended to be used by connections on xDS enabled
  // servers configured with RBAC. The RBAC filter fetches the RBAC policy from
  // the method config of service config returned by the ServerConfigSelector,
  // and enforces the RBAC policy.
  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "rbac_filter"; }

  static absl::StatusOr<std::unique_ptr<RbacFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  RbacFilter(size_t index, const Rbac& rbac,
             EvaluateArgs::PerChannelArgs per_channel_evaluate_args);

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md,
                                         RbacFilter* filter);
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

 private:
  // TODO(roth): Remove these fields as part of removing the
  // xds_server_filter_chain_per_route experiment.
  size_t index_;
  const size_t service_config_parser_index_;

  GrpcAuthorizationEngine authorization_engine_;
  // Per channel args used for authorization.
  EvaluateArgs::PerChannelArgs per_channel_evaluate_args_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H
