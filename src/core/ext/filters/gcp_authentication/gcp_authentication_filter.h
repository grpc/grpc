//
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
//

#ifndef GRPC_SRC_CORE_EXT_FILTERS_GCP_AUTHENTICATION_GCP_AUTHENTICATION_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_GCP_AUTHENTICATION_GCP_AUTHENTICATION_FILTER_H

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/filters/gcp_authentication/gcp_authentication_service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/lru_cache.h"

namespace grpc_core {

// xDS GCP Authentication filter.
// https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/gcp_authn_filter
class GcpAuthenticationFilter
    : public ImplementChannelFilter<GcpAuthenticationFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() { return "gcp_authentication_filter"; }

  static absl::StatusOr<std::unique_ptr<GcpAuthenticationFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  GcpAuthenticationFilter(
      const GcpAuthenticationParsedConfig::Config* filter_config,
      RefCountedPtr<const XdsConfig> xds_config);

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& /*md*/,
                                         GcpAuthenticationFilter* filter);
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
  };

 private:
  RefCountedPtr<grpc_call_credentials> GetCallCredentials(
      const std::string& audience);

  const GcpAuthenticationParsedConfig::Config* filter_config_;
  const RefCountedPtr<const XdsConfig> xds_config_;

  Mutex mu_;
  LruCache<std::string /*audience*/, RefCountedPtr<grpc_call_credentials>>
      cache_ ABSL_GUARDED_BY(&mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_GCP_AUTHENTICATION_GCP_AUTHENTICATION_FILTER_H
