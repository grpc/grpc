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

#include "src/core/ext/filters/client_channel/client_channel_service_config.h"

#include <map>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

//
// ClientChannelGlobalParsedConfig::HealthCheckConfig
//

ClientChannelGlobalParsedConfig::HealthCheckConfig::HealthCheckConfig(
    HealthCheckConfig&& other) noexcept
    : service_name(std::move(other.service_name)) {}

ClientChannelGlobalParsedConfig::HealthCheckConfig&
ClientChannelGlobalParsedConfig::HealthCheckConfig::operator=(
    HealthCheckConfig&& other) noexcept {
  service_name = std::move(other.service_name);
  return *this;
}

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

ClientChannelGlobalParsedConfig::ClientChannelGlobalParsedConfig(
    ClientChannelGlobalParsedConfig&& other) noexcept
    : parsed_lb_config_(std::move(other.parsed_lb_config_)),
      parsed_deprecated_lb_policy_(
          std::move(other.parsed_deprecated_lb_policy_)),
      health_check_config_(std::move(other.health_check_config_)) {}

ClientChannelGlobalParsedConfig& ClientChannelGlobalParsedConfig::operator=(
    ClientChannelGlobalParsedConfig&& other) noexcept {
  parsed_lb_config_ = std::move(other.parsed_lb_config_);
  parsed_deprecated_lb_policy_ = std::move(other.parsed_deprecated_lb_policy_);
  health_check_config_ = std::move(other.health_check_config_);
  return *this;
}

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
                                                   ErrorList* errors) {
  // Parse LB config.
  {
    ScopedField field(errors, ".loadBalancingConfig");
    auto it = json.object_value().find("loadBalancingConfig");
    if (it != json.object_value().end()) {
      auto config =
          LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(it->second);
      if (!config.ok()) {
        errors->AddError(config.status().message());
      } else {
        parsed_lb_config_ = std::move(*config);
      }
    }
  }
  // Sanity-check deprecated "loadBalancingPolicy" field.
  if (!parsed_deprecated_lb_policy_.empty()) {
    ScopedField field(errors, ".loadBalancingPolicy");
    // Convert to lower-case.
    absl::AsciiStrToLower(&parsed_deprecated_lb_policy_);
    bool requires_config = false;
    if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(
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

ClientChannelMethodParsedConfig::ClientChannelMethodParsedConfig(
    ClientChannelMethodParsedConfig&& other) noexcept
    : timeout_(other.timeout_), wait_for_ready_(other.wait_for_ready_) {}

ClientChannelMethodParsedConfig& ClientChannelMethodParsedConfig::operator=(
    ClientChannelMethodParsedConfig&& other) noexcept {
  timeout_ = other.timeout_;
  wait_for_ready_ = other.wait_for_ready_;
  return *this;
}

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
      absl::make_unique<ClientChannelServiceConfigParser>());
}

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
ClientChannelServiceConfigParser::ParseGlobalParams(const ChannelArgs& /*args*/,
                                                    const Json& json) {
  auto global_params = LoadFromJson<ClientChannelGlobalParsedConfig>(json);
  if (!global_params.ok()) return global_params.status();
  return absl::make_unique<ClientChannelGlobalParsedConfig>(
      std::move(*global_params));
}

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
ClientChannelServiceConfigParser::ParsePerMethodParams(
    const ChannelArgs& /*args*/, const Json& json) {
  auto method_params = LoadFromJson<ClientChannelMethodParsedConfig>(json);
  if (!method_params.ok()) return method_params.status();
  return absl::make_unique<ClientChannelMethodParsedConfig>(
      std::move(*method_params));
}

}  // namespace internal
}  // namespace grpc_core
