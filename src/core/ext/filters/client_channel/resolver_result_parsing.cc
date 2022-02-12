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

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/json/json_util.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

namespace {
size_t g_client_channel_service_config_parser_index;
}

size_t ClientChannelServiceConfigParser::ParserIndex() {
  return g_client_channel_service_config_parser_index;
}

void ClientChannelServiceConfigParser::Register() {
  g_client_channel_service_config_parser_index =
      ServiceConfigParser::RegisterParser(
          absl::make_unique<ClientChannelServiceConfigParser>());
}

namespace {

absl::optional<std::string> ParseHealthCheckConfig(const Json& field,
                                                   grpc_error_handle* error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  if (field.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:healthCheckConfig error:should be of type object");
    return absl::nullopt;
  }
  std::vector<grpc_error_handle> error_list;
  absl::optional<std::string> service_name;
  auto it = field.object_value().find("serviceName");
  if (it != field.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:serviceName error:should be of type string"));
    } else {
      service_name = it->second.string_value();
    }
  }
  *error =
      GRPC_ERROR_CREATE_FROM_VECTOR("field:healthCheckConfig", &error_list);
  return service_name;
}

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParseGlobalParams(
    const grpc_channel_args* /*args*/, const Json& json,
    grpc_error_handle* error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  std::vector<grpc_error_handle> error_list;
  // Parse LB config.
  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config;
  auto it = json.object_value().find("loadBalancingConfig");
  if (it != json.object_value().end()) {
    grpc_error_handle parse_error = GRPC_ERROR_NONE;
    parsed_lb_config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
        it->second, &parse_error);
    if (parse_error != GRPC_ERROR_NONE) {
      std::vector<grpc_error_handle> lb_errors;
      lb_errors.push_back(parse_error);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "field:loadBalancingConfig", &lb_errors));
    }
  }
  // Parse deprecated LB policy.
  std::string lb_policy_name;
  it = json.object_value().find("loadBalancingPolicy");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:type should be string"));
    } else {
      lb_policy_name = it->second.string_value();
      for (size_t i = 0; i < lb_policy_name.size(); ++i) {
        lb_policy_name[i] = tolower(lb_policy_name[i]);
      }
      bool requires_config = false;
      if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(
              lb_policy_name.c_str(), &requires_config)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:loadBalancingPolicy error:Unknown lb policy"));
      } else if (requires_config) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("field:loadBalancingPolicy error:", lb_policy_name,
                         " requires a config. Please use loadBalancingConfig "
                         "instead.")));
      }
    }
  }
  // Parse health check config.
  absl::optional<std::string> health_check_service_name;
  it = json.object_value().find("healthCheckConfig");
  if (it != json.object_value().end()) {
    grpc_error_handle parsing_error = GRPC_ERROR_NONE;
    health_check_service_name =
        ParseHealthCheckConfig(it->second, &parsing_error);
    if (parsing_error != GRPC_ERROR_NONE) {
      error_list.push_back(parsing_error);
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("Client channel global parser",
                                         &error_list);
  if (*error == GRPC_ERROR_NONE) {
    return absl::make_unique<ClientChannelGlobalParsedConfig>(
        std::move(parsed_lb_config), std::move(lb_policy_name),
        std::move(health_check_service_name));
  }
  return nullptr;
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParsePerMethodParams(
    const grpc_channel_args* /*args*/, const Json& json,
    grpc_error_handle* error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  std::vector<grpc_error_handle> error_list;
  // Parse waitForReady.
  absl::optional<bool> wait_for_ready;
  auto it = json.object_value().find("waitForReady");
  if (it != json.object_value().end()) {
    if (it->second.type() == Json::Type::JSON_TRUE) {
      wait_for_ready.emplace(true);
    } else if (it->second.type() == Json::Type::JSON_FALSE) {
      wait_for_ready.emplace(false);
    } else {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:waitForReady error:Type should be true/false"));
    }
  }
  // Parse timeout.
  grpc_millis timeout = 0;
  ParseJsonObjectFieldAsDuration(json.object_value(), "timeout", &timeout,
                                 &error_list, false);
  // Return result.
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("Client channel parser", &error_list);
  if (*error == GRPC_ERROR_NONE) {
    return absl::make_unique<ClientChannelMethodParsedConfig>(timeout,
                                                              wait_for_ready);
  }
  return nullptr;
}

}  // namespace internal
}  // namespace grpc_core
