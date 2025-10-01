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
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/filter/blackboard.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/lru_cache.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {

// xDS GCP Authentication filter.
// https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/gcp_authn_filter
class GcpAuthenticationFilter
    : public ImplementChannelFilter<GcpAuthenticationFilter> {
 public:
  struct Config : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE(
          "envoy.extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    std::string instance_name;
    uint64_t cache_size = 10;
  };

  class CallCredentialsCache : public Blackboard::Entry {
   public:
    explicit CallCredentialsCache(size_t max_size) : cache_(max_size) {}

    static UniqueTypeName Type();

    void SetMaxSize(size_t max_size);

    RefCountedPtr<grpc_call_credentials> Get(const std::string& audience);

   private:
    Mutex mu_;
    LruCache<std::string /*audience*/, RefCountedPtr<grpc_call_credentials>>
        cache_ ABSL_GUARDED_BY(&mu_);
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "gcp_authentication_filter"; }

  static absl::StatusOr<std::unique_ptr<GcpAuthenticationFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& /*md*/,
                                         GcpAuthenticationFilter* filter);
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnFinalize;
  };

 private:
  GcpAuthenticationFilter(RefCountedPtr<const Config> filter_config,
                          RefCountedPtr<const XdsConfig> xds_config,
                          RefCountedPtr<CallCredentialsCache> cache);

  const RefCountedPtr<const Config> filter_config_;
  const RefCountedPtr<const XdsConfig> xds_config_;
  const RefCountedPtr<CallCredentialsCache> cache_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_GCP_AUTHENTICATION_GCP_AUTHENTICATION_FILTER_H
