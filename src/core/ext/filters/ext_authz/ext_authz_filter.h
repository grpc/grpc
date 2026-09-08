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
#include <variant>
#include <vector>

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// xDS External Authentication filter.
class ExtAuthzFilter : public ImplementChannelFilter<ExtAuthzFilter> {
 public:
  class ExtAuthzChannel final : public Blackboard::Entry {
   public:
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_authz_channel");
    }
    ExtAuthzChannel(GrpcXdsServerTarget server,
                    RefCountedPtr<XdsTransportFactory::XdsTransport> transport);
    ~ExtAuthzChannel() override;
    const GrpcXdsServerTarget& server() const { return server_; }

    RefCountedPtr<XdsTransportFactory::XdsTransport> transport() const {
      return transport_;
    }

   private:
    GrpcXdsServerTarget server_;
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  };

  struct Config : public FilterConfig {
    enum class CheckResult {
      kSendRequestToExtAuthzService,
      kPassThrough,
      kDeny,
    };

    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_authz_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    bool isHeaderAllowed(absl::string_view key) const;
    CheckResult CheckRequestAllowed() const;

    std::string instance_name;

    // The gRPC service configuration (target URI, credentials, timeout)
    // used to establish the side-channel connection to the external authz
    // server as described in gRFC A102.
    // Or, a ref-counted handle to the persistent gRPC side-channel
    // (ExtAuthzChannel) used to communicate with the external authz server
    // across multiple data plane RPCs.
    // Holds a null RefCountedPtr when neither is set (e.g. in an empty
    // override config).
    std::variant<RefCountedPtr<ExtAuthzChannel>, GrpcXdsServerTarget>
        channel_info;

    // Fractional percent of requests for which filter is enabled, in parts per
    // million.
    std::optional<uint32_t> filter_enabled;

    bool deny_at_disable = false;
    bool failure_mode_allow = false;
    bool failure_mode_allow_header_add = false;
    grpc_status_code status_on_error = GRPC_STATUS_PERMISSION_DENIED;

    std::vector<StringMatcher> allowed_headers;
    std::vector<StringMatcher> disallowed_headers;

    std::optional<HeaderMutationRules> decoder_header_mutation_rules;
    bool include_peer_certificate = false;

    RefCountedPtr<ExtAuthzChannel> channel() const {
      auto* channel =
          std::get_if<RefCountedPtr<ExtAuthzChannel>>(&channel_info);
      if (channel == nullptr) return nullptr;
      return *channel;
    }
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

  RefCountedPtr<ExtAuthzClient> client() const { return client_; }
  RefCountedPtr<ExtAuthzChannel> channel() const {
    return config_->channel();
  }

 private:
  explicit ExtAuthzFilter(RefCountedPtr<const Config> config);

  const RefCountedPtr<const Config> config_;
  RefCountedPtr<ExtAuthzClient> client_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H