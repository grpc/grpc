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

#include <grpc/status.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"

namespace grpc_core {

struct ExtAuthz : public RefCounted<ExtAuthz> {
  std::optional<GrpcXdsServerTarget> server_target;
  std::string server_uri;

  // Fractional percent of requests for which filter is enabled, in parts per million.
  std::optional<uint32_t> filter_enabled;

  bool deny_at_disable = false;
  bool failure_mode_allow = false;
  bool failure_mode_allow_header_add = false;
  grpc_status_code status_on_error = GRPC_STATUS_PERMISSION_DENIED;

  std::vector<StringMatcher> allowed_headers;
  std::vector<StringMatcher> disallowed_headers;

  bool isHeaderAllowed(absl::string_view key) const;

  std::optional<HeaderMutationRules> decoder_header_mutation_rules;
  bool include_peer_certificate = false;

  bool operator==(const ExtAuthz& other) const;
  bool operator!=(const ExtAuthz& other) const { return !(*this == other); }

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
  class ChannelCache final : public Blackboard::Entry {
   public:
    static UniqueTypeName Type();

    ChannelCache(std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
                 RefCountedPtr<XdsTransportFactory> transport_factory)
        : server_(std::move(server)),
          client_(MakeRefCounted<ExtAuthzClient>(
              std::move(transport_factory),
              std::make_unique<GrpcXdsServerTarget>(
                  *DownCast<const GrpcXdsServerTarget*>(server_.get())))) {}

    ChannelCache(RefCountedPtr<ExtAuthzClient> client,
                 std::shared_ptr<const XdsBootstrap::XdsServerTarget> server)
        : server_(std::move(server)), client_(std::move(client)) {}

    RefCountedPtr<ExtAuthzClient> client() const { return client_; }
    RefCountedPtr<ExtAuthzClient> Get() const { return client_; }

    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server() const {
      return server_;
    }

   private:
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server_;
    RefCountedPtr<ExtAuthzClient> client_;
  };

  struct Config : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_authz_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    std::string instance_name;
    RefCountedPtr<ExtAuthz> ext_authz;
    RefCountedPtr<ChannelCache> channel_cache;
    bool disabled = false;
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

   private:
    std::optional<std::vector<XdsHeaderValueOption>> response_headers_to_add;
    std::optional<std::vector<XdsHeaderValueOption>> response_trailer_to_add;
  };

 private:
  explicit ExtAuthzFilter(RefCountedPtr<const Config> filter_config);

  const RefCountedPtr<const Config> filter_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H