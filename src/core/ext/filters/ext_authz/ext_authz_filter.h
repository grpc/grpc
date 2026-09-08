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
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_authz_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

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
    // Fractional percent of requests for which the filter is enabled, in parts
    // per million (capped at 100%). Optional; if unset, the filter is enabled.
    std::optional<uint32_t> filter_enabled;
    // Optional; if unset (false), requests are allowed when the filter is
    // disabled. If true, then when the filter is disabled, the request will be
    // failed with a status based on status_on_error.
    bool deny_at_disable = false;
    // When set to true, requests will be allowed even if communication with the
    // authorization service has failed, or if the authorization service has
    // returned an HTTP 5xx error. Defaults to false.
    bool failure_mode_allow = false;
    // When failure_mode_allow and failure_mode_allow_header_add are both set to
    // true, 'x-envoy-auth-failure-mode-allowed: true' will be added to request
    // headers if communication with the authorization service has failed, or
    // if the authorization service has returned an HTTP 5xx error.
    bool failure_mode_allow_header_add = false;
    // Status to return when the authorization service returns an error or when
    // communication fails (if failure_mode_allow is false) or when the filter
    // is disabled and deny_at_disable is true. Note that the proto specifies an
    // HTTP status code, not a gRPC status code; this field stores the gRPC
    // status code determined using the normal HTTP-to-gRPC status conversion
    // rules.
    grpc_status_code status_on_error = GRPC_STATUS_PERMISSION_DENIED;
    // Matchers for client request headers that are allowed to be forwarded to
    // the authorization server. An empty list is treated the same as unset.
    std::vector<StringMatcher> allowed_headers;
    // Matchers for client request headers that are disallowed from being
    // forwarded to the authorization server. An empty list is treated the same
    // as unset. Takes precedence over allowed_headers.
    std::vector<StringMatcher> disallowed_headers;
    // Optional rules governing header mutations that the authorization service
    // is allowed to perform on the request headers forwarded upstream.
    // Validated as described in gRFC A102.
    std::optional<HeaderMutationRules> decoder_header_mutation_rules;
    // Whether to send the client certificate to the authorization service in
    // the CheckRequest.
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

  RefCountedPtr<ExtAuthzChannel> channel() const { return config_->channel(); }

 private:
  explicit ExtAuthzFilter(RefCountedPtr<const Config> config);

  const RefCountedPtr<const Config> config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_FILTER_H