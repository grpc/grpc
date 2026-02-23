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

#include <memory>
#include <optional>

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "absl/random/bit_gen_ref.h"

namespace grpc_core {

struct ExtAuthz : public RefCounted<ExtAuthz> {
  std::shared_ptr<XdsGrpcService> xds_grpc_service;
  std::string server_uri;
  RefCountedPtr<XdsTransportFactory> transport_factory;

  struct FilterEnabled {
    uint32_t numerator;
    int32_t denominator;  // 100, 10000, 1000000

    bool operator==(const FilterEnabled& other) const {
      return numerator == other.numerator && denominator == other.denominator;
    }
  };
  std::optional<FilterEnabled> filter_enabled;

  std::optional<bool> deny_at_disable = true;
  bool failure_mode_allow;
  bool failure_mode_allow_header_add;
  grpc_status_code status_on_error;

  std::vector<StringMatcher> allowed_headers;
  std::vector<StringMatcher> disallowed_headers;

  bool isHeaderAllowed(std::string key) const;

  std::optional<HeaderMutationRules> decoder_header_mutation_rules;
  bool include_peer_certificate = false;

  bool operator==(const ExtAuthz& other) const;

  enum class CheckResult {
    kSendRequestToExtAuthzService,
    kPassThrough,
    kDeny,
  };

  CheckResult CheckRequestAllowed() const;
};

// xDS External Authentication filter.
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
    RefCountedPtr<ExtAuthz> ext_authz;
  };

  class ChannelCache : public Blackboard::Entry {
   public:
    ChannelCache(RefCountedPtr<ExtAuthzClient> client,
                 std::shared_ptr<XdsGrpcService> server)
        : client_(client), server_(server) {}

    static UniqueTypeName Type();

    RefCountedPtr<ExtAuthzClient> Get() const;
    void Remove();

    std::shared_ptr<XdsGrpcService> server() const { return server_; }

   private:
    mutable Mutex mu_;
    mutable RefCountedPtr<ExtAuthzClient> client_ ABSL_GUARDED_BY(&mu_);
    mutable std::shared_ptr<XdsGrpcService> server_;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "ext_authz_filter"; }

  static absl::StatusOr<std::unique_ptr<ExtAuthzFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  class Call {
   public:
    ServerMetadataHandle OnClientInitialMetadata(ClientMetadata& md,
                                                 ExtAuthzFilter* filter);
    absl::Status OnServerInitialMetadata(ServerMetadata& md,
                                         ExtAuthzFilter* filter);
    absl::Status OnServerTrailingMetadata(ServerMetadata& md,
                                          ExtAuthzFilter* filter);
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

 private:
  ExtAuthzFilter(RefCountedPtr<const Config> filter_config,
                 RefCountedPtr<const ChannelCache> channel_cache);

  const RefCountedPtr<const Config> filter_config_;
  const RefCountedPtr<const ChannelCache> channel_cache_;
  std::optional<std::vector<HeaderValueOption>> response_headers_to_add;
  std::optional<std::vector<HeaderValueOption>> response_trailer_to_add;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H