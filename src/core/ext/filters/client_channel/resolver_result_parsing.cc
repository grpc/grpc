/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

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
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/json/json_util.h"
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

std::unique_ptr<ClientChannelMethodParsedConfig::RetryPolicy> ParseRetryPolicy(
    const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  auto retry_policy =
      absl::make_unique<ClientChannelMethodParsedConfig::RetryPolicy>();
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryPolicy error:should be of type object");
    return nullptr;
  }
  std::vector<grpc_error*> error_list;
  // Parse maxAttempts.
  auto it = json.object_value().find("maxAttempts");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:maxAttempts error:should be of type number"));
    } else {
      retry_policy->max_attempts =
          gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (retry_policy->max_attempts <= 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxAttempts error:should be at least 2"));
      } else if (retry_policy->max_attempts > MAX_MAX_RETRY_ATTEMPTS) {
        gpr_log(GPR_ERROR,
                "service config: clamped retryPolicy.maxAttempts at %d",
                MAX_MAX_RETRY_ATTEMPTS);
        retry_policy->max_attempts = MAX_MAX_RETRY_ATTEMPTS;
      }
    }
  }
  // Parse initialBackoff.
  if (ParseJsonObjectFieldAsDuration(json.object_value(), "initialBackoff",
                                     &retry_policy->initial_backoff,
                                     &error_list) &&
      retry_policy->initial_backoff == 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:initialBackoff error:must be greater than 0"));
  }
  // Parse maxBackoff.
  if (ParseJsonObjectFieldAsDuration(json.object_value(), "maxBackoff",
                                     &retry_policy->max_backoff, &error_list) &&
      retry_policy->max_backoff == 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:maxBackoff error:should be greater than 0"));
  }
  // Parse backoffMultiplier.
  it = json.object_value().find("backoffMultiplier");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:backoffMultiplier error:should be of type number"));
    } else {
      if (sscanf(it->second.string_value().c_str(), "%f",
                 &retry_policy->backoff_multiplier) != 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:failed to parse"));
      } else if (retry_policy->backoff_multiplier <= 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:should be greater than 0"));
      }
    }
  }
  // Parse retryableStatusCodes.
  it = json.object_value().find("retryableStatusCodes");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryableStatusCodes error:should be of type array"));
    } else {
      for (const Json& element : it->second.array_value()) {
        if (element.type() != Json::Type::STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:retryableStatusCodes error:status codes should be of type "
              "string"));
          continue;
        }
        grpc_status_code status;
        if (!grpc_status_code_from_string(element.string_value().c_str(),
                                          &status)) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:retryableStatusCodes error:failed to parse status code"));
          continue;
        }
        retry_policy->retryable_status_codes.Add(status);
      }
      if (retry_policy->retryable_status_codes.Empty()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryableStatusCodes error:should be non-empty"));
      };
    }
  }
  // Make sure required fields are set.
  if (error_list.empty()) {
    if (retry_policy->max_attempts == 0 || retry_policy->initial_backoff == 0 ||
        retry_policy->max_backoff == 0 ||
        retry_policy->backoff_multiplier == 0 ||
        retry_policy->retryable_status_codes.Empty()) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryPolicy error:Missing required field(s)");
      return nullptr;
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("retryPolicy", &error_list);
  return *error == GRPC_ERROR_NONE ? std::move(retry_policy) : nullptr;
}

grpc_error* ParseRetryThrottling(
    const Json& json,
    ClientChannelGlobalParsedConfig::RetryThrottling* retry_throttling) {
  if (json.type() != Json::Type::OBJECT) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling error:Type should be object");
  }
  std::vector<grpc_error*> error_list;
  // Parse maxTokens.
  auto it = json.object_value().find("maxTokens");
  if (it == json.object_value().end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling field:maxTokens error:Not found"));
  } else if (it->second.type() != Json::Type::NUMBER) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling field:maxTokens error:Type should be "
        "number"));
  } else {
    retry_throttling->max_milli_tokens =
        gpr_parse_nonnegative_int(it->second.string_value().c_str()) * 1000;
    if (retry_throttling->max_milli_tokens <= 0) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryThrottling field:maxTokens error:should be "
          "greater than zero"));
    }
  }
  // Parse tokenRatio.
  it = json.object_value().find("tokenRatio");
  if (it == json.object_value().end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling field:tokenRatio error:Not found"));
  } else if (it->second.type() != Json::Type::NUMBER) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling field:tokenRatio error:type should be "
        "number"));
  } else {
    // We support up to 3 decimal digits.
    size_t whole_len = it->second.string_value().size();
    const char* value = it->second.string_value().c_str();
    uint32_t multiplier = 1;
    uint32_t decimal_value = 0;
    const char* decimal_point = strchr(value, '.');
    if (decimal_point != nullptr) {
      whole_len = static_cast<size_t>(decimal_point - value);
      multiplier = 1000;
      size_t decimal_len = strlen(decimal_point + 1);
      if (decimal_len > 3) decimal_len = 3;
      if (!gpr_parse_bytes_to_uint32(decimal_point + 1, decimal_len,
                                     &decimal_value)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryThrottling field:tokenRatio error:Failed "
            "parsing"));
        return GRPC_ERROR_CREATE_FROM_VECTOR("retryPolicy", &error_list);
      }
      uint32_t decimal_multiplier = 1;
      for (size_t i = 0; i < (3 - decimal_len); ++i) {
        decimal_multiplier *= 10;
      }
      decimal_value *= decimal_multiplier;
    }
    uint32_t whole_value;
    if (!gpr_parse_bytes_to_uint32(value, whole_len, &whole_value)) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryThrottling field:tokenRatio error:Failed "
          "parsing"));
      return GRPC_ERROR_CREATE_FROM_VECTOR("retryPolicy", &error_list);
    }
    retry_throttling->milli_token_ratio =
        static_cast<int>((whole_value * multiplier) + decimal_value);
    if (retry_throttling->milli_token_ratio <= 0) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryThrottling field:tokenRatio error:value should "
          "be greater than 0"));
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("retryPolicy", &error_list);
}

absl::optional<std::string> ParseHealthCheckConfig(const Json& field,
                                                   grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  if (field.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:healthCheckConfig error:should be of type object");
    return absl::nullopt;
  }
  std::vector<grpc_error*> error_list;
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

uint32_t ParsePerMillionField(const Json& json, const char* name,
                              std::vector<grpc_error*>* error_list) {
  auto it = json.object_value().find(name);
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", name, " error:should be of type number")
              .c_str()));
      return 0;
    }
    const uint32_t candidate =
        gpr_parse_nonnegative_int(it->second.string_value().c_str());
    if (candidate < 0) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", name, " error:should be nonnegative number")
              .c_str()));
      return 0;
    }
    return GPR_MIN(candidate, 1000000);
  }
  return 0;
}

// TODO(lidiz) merge with new helpers in src/core/lib/json/json_util.h
std::string ParseStringField(const Json& json, const char* name,
                             std::vector<grpc_error*>* error_list) {
  auto it = json.object_value().find(name);
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", name, " error:should be of type string")
              .c_str()));
      return "";
    }
    return it->second.string_value().c_str();
  }
  return "";
}

std::unique_ptr<ClientChannelMethodParsedConfig::FaultInjectionPolicy>
ParseFaultInjectionPolicy(const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  auto fault_injection_policy = absl::make_unique<
      ClientChannelMethodParsedConfig::FaultInjectionPolicy>();
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:faultInjectionPolicy error:should be of type object");
    return nullptr;
  }
  std::vector<grpc_error*> error_list;
  // Parse abort_per_million
  fault_injection_policy->abort_per_million =
      ParsePerMillionField(json, "abortPerMillion", &error_list);
  // Parse abort_code
  auto it = json.object_value().find("abortCode");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:abortCode error:should be of type string"));
    } else if (!grpc_status_code_from_string(
                   it->second.string_value().c_str(),
                   &(fault_injection_policy->abort_code))) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:abortCode error:failed to parse status code"));
    }
  }
  // Parse abort_message
  it = json.object_value().find("abortMessage");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field: abortMessage error:should be of type string")
              .c_str()));
    } else {
      fault_injection_policy->abort_message = it->second.string_value();
    }
  } else {
    fault_injection_policy->abort_message = "Fault injected";
  }
  // Parse abort_code_header
  fault_injection_policy->abort_code_header =
      ParseStringField(json, "abortCodeHeader", &error_list);
  // Parse abort_per_million_header
  fault_injection_policy->abort_per_million_header =
      ParseStringField(json, "abortPerMillionHeader", &error_list);
  // Parse delay_per_million
  fault_injection_policy->delay_per_million =
      ParsePerMillionField(json, "delayPerMillion", &error_list);
  // Parse delay
  it = json.object_value().find("delay");
  if (it != json.object_value().end()) {
    if (!ParseDurationFromJson(it->second, &fault_injection_policy->delay)) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:delay error:Failed parsing"));
    };
  }
  // Parse delay_header
  fault_injection_policy->delay_header =
      ParseStringField(json, "delayHeader", &error_list);
  // Parse delay_per_million_header
  fault_injection_policy->delay_per_million_header =
      ParseStringField(json, "delayPerMillionHeader", &error_list);
  // Parse max_faults
  it = json.object_value().find("maxFaults");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:maxFaults error:should be of type number"));
    } else {
      fault_injection_policy->max_faults =
          gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (fault_injection_policy->max_faults < 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxFaults error:should be zero or positive"));
      }
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("faultInjectionPolicy", &error_list);
  return *error == GRPC_ERROR_NONE ? std::move(fault_injection_policy)
                                   : nullptr;
}

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParseGlobalParams(
    const grpc_channel_args* /*args*/, const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  std::vector<grpc_error*> error_list;
  // Parse LB config.
  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config;
  auto it = json.object_value().find("loadBalancingConfig");
  if (it != json.object_value().end()) {
    grpc_error* parse_error = GRPC_ERROR_NONE;
    parsed_lb_config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
        it->second, &parse_error);
    if (parsed_lb_config == nullptr) {
      std::vector<grpc_error*> lb_errors;
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
        error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("field:loadBalancingPolicy error:", lb_policy_name,
                         " requires a config. Please use loadBalancingConfig "
                         "instead.")
                .c_str()));
      }
    }
  }
  // Parse retry throttling.
  absl::optional<ClientChannelGlobalParsedConfig::RetryThrottling>
      retry_throttling;
  it = json.object_value().find("retryThrottling");
  if (it != json.object_value().end()) {
    ClientChannelGlobalParsedConfig::RetryThrottling data;
    grpc_error* parsing_error = ParseRetryThrottling(it->second, &data);
    if (parsing_error != GRPC_ERROR_NONE) {
      error_list.push_back(parsing_error);
    } else {
      retry_throttling.emplace(data);
    }
  }
  // Parse health check config.
  absl::optional<std::string> health_check_service_name;
  it = json.object_value().find("healthCheckConfig");
  if (it != json.object_value().end()) {
    grpc_error* parsing_error = GRPC_ERROR_NONE;
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
        retry_throttling, std::move(health_check_service_name));
  }
  return nullptr;
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ClientChannelServiceConfigParser::ParsePerMethodParams(
    const grpc_channel_args* args, const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  std::vector<grpc_error*> error_list;
  absl::optional<bool> wait_for_ready;
  grpc_millis timeout = 0;
  std::unique_ptr<ClientChannelMethodParsedConfig::RetryPolicy> retry_policy;
  std::unique_ptr<ClientChannelMethodParsedConfig::FaultInjectionPolicy>
      fault_injection_policy;
  // Parse waitForReady.
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
  ParseJsonObjectFieldAsDuration(json.object_value(), "timeout", &timeout,
                                 &error_list, false);
  // Parse retry policy.
  it = json.object_value().find("retryPolicy");
  if (it != json.object_value().end()) {
    grpc_error* error = GRPC_ERROR_NONE;
    retry_policy = ParseRetryPolicy(it->second, &error);
    if (retry_policy == nullptr) {
      error_list.push_back(error);
    }
  }
  // Parse fault injection policy.
  if (grpc_channel_args_find_bool(
          args, GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG, false)) {
    it = json.object_value().find("faultInjectionPolicy");
    if (it != json.object_value().end()) {
      grpc_error* error = GRPC_ERROR_NONE;
      fault_injection_policy = ParseFaultInjectionPolicy(it->second, &error);
      if (fault_injection_policy == nullptr) {
        error_list.push_back(error);
      }
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("Client channel parser", &error_list);
  if (*error == GRPC_ERROR_NONE) {
    return absl::make_unique<ClientChannelMethodParsedConfig>(
        timeout, wait_for_ready, std::move(retry_policy),
        std::move(fault_injection_policy));
  }
  return nullptr;
}

}  // namespace internal
}  // namespace grpc_core
