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

#include <grpc/support/port_platform.h>

#include "src/core/client_channel/client_channel_service_config.h"

#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include "src/core/load_balancing/lb_policy_registry.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

//
// ClientChannelGlobalParsedConfig::HealthCheckConfig
//

const JsonLoaderInterface*
ClientChannelGlobalParsedConfig::HealthCheckConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<HealthCheckConfig>()
          .OptionalField("serviceName", &HealthCheckConfig::service_name)
          .Finish();
  return loader;
}

//
// ClientChannelGlobalParsedConfig
//

const JsonLoaderInterface* ClientChannelGlobalParsedConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ClientChannelGlobalParsedConfig>()
          // Note: "loadBalancingConfig" requires special handling, so
          // that field will be parsed in JsonPostLoad() instead.
          .OptionalField(
              "loadBalancingPolicy",
              &ClientChannelGlobalParsedConfig::parsed_deprecated_lb_policy_)
          .OptionalField("healthCheckConfig",
                         &ClientChannelGlobalParsedConfig::health_check_config_)
          .Finish();
  return loader;
}

void ClientChannelGlobalParsedConfig::JsonPostLoad(const Json& json,
                                                   const JsonArgs&,
                                                   ValidationErrors* errors) {
  const auto& lb_policy_registry =
      CoreConfiguration::Get().lb_policy_registry();
  // Parse LB config.
  {
    ValidationErrors::ScopedField field(errors, ".loadBalancingConfig");
    auto it = json.object().find("loadBalancingConfig");
    if (it != json.object().end()) {
      auto config = lb_policy_registry.ParseLoadBalancingConfig(it->second);
      if (!config.ok()) {
        errors->AddError(config.status().message());
      } else {
        parsed_lb_config_ = std::move(*config);
      }
    }
  }
  // Sanity-check deprecated "loadBalancingPolicy" field.
  if (!parsed_deprecated_lb_policy_.empty()) {
    ValidationErrors::ScopedField field(errors, ".loadBalancingPolicy");
    // Convert to lower-case.
    absl::AsciiStrToLower(&parsed_deprecated_lb_policy_);
    bool requires_config = false;
    if (!lb_policy_registry.LoadBalancingPolicyExists(
            parsed_deprecated_lb_policy_, &requires_config)) {
      errors->AddError(absl::StrCat("unknown LB policy \"",
                                    parsed_deprecated_lb_policy_, "\""));
    } else if (requires_config) {
      errors->AddError(absl::StrCat(
          "LB policy \"", parsed_deprecated_lb_policy_,
          "\" requires a config. Please use loadBalancingConfig instead."));
    }
  }
}

//
// ClientChannelMethodParsedConfig
//

const JsonLoaderInterface* ClientChannelMethodParsedConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ClientChannelMethodParsedConfig>()
          .OptionalField("timeout", &ClientChannelMethodParsedConfig::timeout_)
          .OptionalField("waitForReady",
                         &ClientChannelMethodParsedConfig::wait_for_ready_)
          .Finish();
  return loader;
}

//
// ClientChannelServiceConfigParser
//

size_t ClientChannelServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

void ClientChannelServiceConfigParser::Register(
    CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<ClientChannelServiceConfigParser>());
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParseGlobalParams(const ChannelArgs& /*args*/,
                                                    const Json& json,
                                                    ValidationErrors* errors) {
  return LoadFromJson<std::unique_ptr<ClientChannelGlobalParsedConfig>>(
      json, JsonArgs(), errors);
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParsePerMethodParams(
    const ChannelArgs& /*args*/, const Json& json, ValidationErrors* errors) {
  return LoadFromJson<std::unique_ptr<ClientChannelMethodParsedConfig>>(
      json, JsonArgs(), errors);
}

}  // namespace internal
}  // namespace grpc_core
