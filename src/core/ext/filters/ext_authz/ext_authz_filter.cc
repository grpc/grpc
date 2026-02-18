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

#include "src/core/ext/filters/ext_authz/ext_authz_filter.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include <cstddef>
#include <memory>
#include <utility>

// #include "src/core/config/core_configuration.h"
// #include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/ext/filters/ext_authz/ext_authz_service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
// #include "src/core/xds/grpc/xds_client_grpc.h"
// #include "src/core/lib/promise/seq.h"
// #include "src/core/lib/transport/status_conversion.h"
#include "src/core/service_config/service_config_call_data.h"
// #include "absl/random/random.h"
#include <string>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtAuthzFilter
//

const grpc_channel_filter ExtAuthzFilter::kFilterVtable =
    MakePromiseBasedFilter<ExtAuthzFilter, FilterEndpoint::kClient, 0>();

absl::StatusOr<std::unique_ptr<ExtAuthzFilter>> ExtAuthzFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  // Get filter config.
  auto service_config = args.GetObjectRef<ServiceConfig>();
  if (service_config == nullptr) {
    return absl::InvalidArgumentError(
        "ext_authz: no service config in channel args");
  }
  auto* config = static_cast<const ExtAuthzParsedConfig*>(
      service_config->GetGlobalParsedConfig(
          ExtAuthzServiceConfigParser::ParserIndex()));
  if (config == nullptr) {
    return absl::InvalidArgumentError("ext_authz: parsed config not found");
  }
  auto* filter_config = config->GetConfig(filter_args.instance_id());
  if (filter_config == nullptr) {
    return absl::InvalidArgumentError(
        "ext_authz: filter instance ID not found in filter config");
  }
  std::optional<absl::string_view> server_uri =
      args.GetString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    return absl::InvalidArgumentError(
        "ext_authz: no server URI in channel args");
  }
  auto xds_client = GrpcXdsClient::GetOrCreate(*server_uri, args,
                                               "ext_authz_filter_create");
  if (!xds_client.ok()) {
    return xds_client.status();
  }
  // auto ext_authz_client = MakeRefCounted<ExtAuthzClient>(
  //     (*xds_client)->bootstrap_ptr(), (*xds_client)->transport_factory()->Ref(),
  //     (*xds_client)->engine_ptr());
  // return std::make_unique<ExtAuthzFilter>(filter_config,
  //                                         std::move(ext_authz_client));
  return nullptr;
}

// RefCountedPtr<Channel> ExtAuthzFilter::ChannelCache::GetChannel(
//     const XdsGrpcService& service) {
//   MutexLock lock(&mu_);
//   auto it = channels_.find(service.target_uri);
//   if (it != channels_.end()) {
//     return it->second;
//   }

//   // TODO(rishesh): Create channel with credentials from config.
//   // Using Insecure for initial implementation as credentials are not yet
//   part
//   // of GrpcService struct.
//   grpc_channel_credentials* creds =
//   grpc_insecure_channel_credentials_create();
//   // Using internal channel args effectively defaults. We might need to pass
//   // relevant args.
//   grpc_channel* c_channel =
//       grpc_channel_create(service.target_uri.c_str(), creds, nullptr);
//   grpc_channel_credentials_release(creds);

//   // grpc_channel is effectively a Channel*.
//   RefCountedPtr<Channel> channel(reinterpret_cast<Channel*>(c_channel));
//   channels_.emplace(service.target_uri, channel);
//   return channel;
// }

absl::Status ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  std::vector<std::pair<std::string, std::string>> metadata_list;
  md.Log([&](absl::string_view key, absl::string_view value) {
    //  if the header is matched by the disallowed_headers config field, it will
    //  not be added to this map
    if (filter->config_->isHeaderPresentInDisallowedHeaders(std::string(key))) {
      return;
    }
    // if the allowed_headers config field is unset or matches the header, the
    // header will be added to this map.
    if (filter->config_->isHeaderPresentInAllowedHeaders(std::string(key))) {
      metadata_list.emplace_back(std::string(key), std::string(value));
    }
    // Otherwise, the header will be excluded from this map.
  });

  // TODO(rishesh): add the loggic to make the ext_authz_call

  return absl::OkStatus();
}

void ExtAuthzFilterRegister(CoreConfiguration::Builder* builder) {
  ExtAuthzServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
