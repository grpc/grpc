//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_grpc_service_parser.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

GrpcXdsServerTarget ParseXdsGrpcService(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_GrpcService* grpc_service,
    ValidationErrors* errors) {
  if (grpc_service == nullptr) {
    errors->AddError("field not set");
    return GrpcXdsServerTarget(/*server_uri=*/"",
                               /*channel_creds_config=*/nullptr,
                               /*call_creds_configs=*/{});
  }
  // timeout
  Duration timeout = Duration::Infinity();
  if (auto* timeout_proto =
          envoy_config_core_v3_GrpcService_timeout(grpc_service);
      timeout_proto != nullptr) {
    ValidationErrors::ScopedField field(errors, ".timeout");
    timeout = ParseDuration(timeout_proto, errors);
    if (timeout <= Duration::Zero()) {
      errors->AddError("duration must be positive");
    }
  }
  // initial_metadata
  std::vector<std::pair<std::string, std::string>> initial_metadata;
  size_t initial_metadata_size;
  auto* initial_metadata_proto =
      envoy_config_core_v3_GrpcService_initial_metadata(grpc_service,
                                                        &initial_metadata_size);
  for (size_t i = 0; i < initial_metadata_size; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".initial_metadata[", i, "]"));
    initial_metadata.push_back(
        ParseXdsHeader(initial_metadata_proto[i], errors));
  }
  // google_grpc
  ValidationErrors::ScopedField field(errors, ".google_grpc");
  auto* google_grpc =
      envoy_config_core_v3_GrpcService_google_grpc(grpc_service);
  if (google_grpc == nullptr) {
    errors->AddError("field not set");
    return GrpcXdsServerTarget(/*server_uri=*/"",
                               /*channel_creds_config=*/nullptr,
                               /*call_creds_configs=*/{});
  }
  // target_uri
  std::string target_uri = UpbStringToStdString(
      envoy_config_core_v3_GrpcService_GoogleGrpc_target_uri(google_grpc));
  if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(target_uri)) {
    ValidationErrors::ScopedField field(errors, ".target_uri");
    errors->AddError("invalid target URI");
  }
  // credentials
  RefCountedPtr<const ChannelCredsConfig> channel_creds_config;
  std::vector<RefCountedPtr<const CallCredsConfig>> call_creds_configs;
  if (DownCast<const GrpcXdsServer&>(context.server).TrustedXdsServer()) {
    // Trusted xDS server.  Use credentials from the GoogleGrpc proto.
    // First, look at channel creds.
    {
      ValidationErrors::ScopedField field(errors,
                                          ".channel_credentials_plugin");
      size_t size;
      const auto* const* channel_creds_plugin =
          envoy_config_core_v3_GrpcService_GoogleGrpc_channel_credentials_plugin(
              google_grpc, &size);
      if (size == 0) {
        errors->AddError("field not set");
      } else {
        const auto& registry =
            CoreConfiguration::Get().channel_creds_registry();
        const auto& certificate_providers =
            DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
                .certificate_providers();
        for (size_t i = 0; i < size; ++i) {
          ValidationErrors::ScopedField field(errors,
                                              absl::StrCat("[", i, "]"));
          auto extension =
              ExtractXdsExtension(context, channel_creds_plugin[i], errors);
          if (!extension.has_value()) continue;
          if (!registry.IsProtoSupported(extension->type)) continue;
          absl::string_view* serialized_config =
              std::get_if<absl::string_view>(&extension->value);
          if (serialized_config == nullptr) {
            errors->AddError("can't decode config");
            continue;
          }
          channel_creds_config =
              registry.ParseProto(extension->type, *serialized_config,
                                  certificate_providers, errors);
          break;
        }
        if (channel_creds_config == nullptr) {
          errors->AddError("no supported channel credentials type found");
        }
      }
    }
    // Now look at call creds.
    {
      ValidationErrors::ScopedField field(errors, ".call_credentials_plugin");
      size_t size;
      const auto* const* call_creds_plugin =
          envoy_config_core_v3_GrpcService_GoogleGrpc_call_credentials_plugin(
              google_grpc, &size);
      const auto& registry = CoreConfiguration::Get().call_creds_registry();
      for (size_t i = 0; i < size; ++i) {
        ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
        auto extension =
            ExtractXdsExtension(context, call_creds_plugin[i], errors);
        if (!extension.has_value()) continue;
        if (!registry.IsProtoSupported(extension->type)) continue;
        absl::string_view* serialized_config =
            std::get_if<absl::string_view>(&extension->value);
        if (serialized_config == nullptr) {
          errors->AddError("can't decode config");
          continue;
        }
        call_creds_configs.push_back(
            registry.ParseProto(extension->type, *serialized_config, errors));
      }
    }
  } else {
    // Not a trusted xDS server.  Do lookup in bootstrap.
    const auto& bootstrap =
        DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
    auto& allowed_grpc_services = bootstrap.allowed_grpc_services();
    auto it = allowed_grpc_services.find(target_uri);
    if (it == allowed_grpc_services.end()) {
      ValidationErrors::ScopedField field(errors, ".target_uri");
      errors->AddError(
          "service not present in \"allowed_grpc_services\" "
          "in bootstrap config");
    } else {
      channel_creds_config = it->second.channel_creds_config;
      call_creds_configs = it->second.call_creds_configs;
    }
  }
  return GrpcXdsServerTarget(target_uri, std::move(channel_creds_config),
                             std::move(call_creds_configs),
                             std::move(initial_metadata), timeout);
}

}  // namespace grpc_core
