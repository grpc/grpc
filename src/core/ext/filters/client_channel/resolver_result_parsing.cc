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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/health/health_check_parser.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/optional.h"
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
      ServiceConfig::RegisterParser(UniquePtr<ServiceConfig::Parser>(
          New<ClientChannelServiceConfigParser>()));
}

ProcessedResolverResult::ProcessedResolverResult(
    const Resolver::Result& resolver_result)
    : service_config_(resolver_result.service_config) {
  // If resolver did not return a service config, use the default
  // specified via the client API.
  if (service_config_ == nullptr) {
    const char* service_config_json = grpc_channel_arg_get_string(
        grpc_channel_args_find(resolver_result.args, GRPC_ARG_SERVICE_CONFIG));
    if (service_config_json != nullptr) {
      grpc_error* error = GRPC_ERROR_NONE;
      service_config_ = ServiceConfig::Create(service_config_json, &error);
      // Error is currently unused.
      GRPC_ERROR_UNREF(error);
    }
  }
  // Process service config.
  const ClientChannelGlobalParsedObject* parsed_object = nullptr;
  if (service_config_ != nullptr) {
    parsed_object = static_cast<const ClientChannelGlobalParsedObject*>(
        service_config_->GetParsedGlobalServiceConfigObject(
            ClientChannelServiceConfigParser::ParserIndex()));
    ProcessServiceConfig(resolver_result, parsed_object);
  }
  ProcessLbPolicy(resolver_result, parsed_object);
}

void ProcessedResolverResult::ProcessServiceConfig(
    const Resolver::Result& resolver_result,
    const ClientChannelGlobalParsedObject* parsed_object) {
  auto* health_check = static_cast<HealthCheckParsedObject*>(
      service_config_->GetParsedGlobalServiceConfigObject(
          HealthCheckParser::ParserIndex()));
  health_check_service_name_ =
      health_check != nullptr ? health_check->service_name() : nullptr;
  service_config_json_ = service_config_->service_config_json();
  if (!parsed_object) {
    return;
  }
  if (parsed_object->retry_throttling().has_value()) {
    const grpc_arg* channel_arg =
        grpc_channel_args_find(resolver_result.args, GRPC_ARG_SERVER_URI);
    const char* server_uri = grpc_channel_arg_get_string(channel_arg);
    GPR_ASSERT(server_uri != nullptr);
    grpc_uri* uri = grpc_uri_parse(server_uri, true);
    GPR_ASSERT(uri->path[0] != '\0');
    server_name_ = uri->path[0] == '/' ? uri->path + 1 : uri->path;
    retry_throttle_data_ = parsed_object->retry_throttling();
    grpc_uri_destroy(uri);
  }
}

void ProcessedResolverResult::ProcessLbPolicy(
    const Resolver::Result& resolver_result,
    const ClientChannelGlobalParsedObject* parsed_object) {
  // Prefer the LB policy name found in the service config.
  if (parsed_object != nullptr) {
    if (parsed_object->parsed_lb_config() != nullptr) {
      lb_policy_name_.reset(
          gpr_strdup(parsed_object->parsed_lb_config()->name()));
      lb_policy_config_ = parsed_object->parsed_lb_config();
    } else {
      lb_policy_name_.reset(
          gpr_strdup(parsed_object->parsed_deprecated_lb_policy()));
      if (lb_policy_name_ != nullptr) {
        char* lb_policy_name = lb_policy_name_.get();
        for (size_t i = 0; i < strlen(lb_policy_name); ++i) {
          lb_policy_name[i] = tolower(lb_policy_name[i]);
        }
      }
    }
  }
  // Otherwise, find the LB policy name set by the client API.
  if (lb_policy_name_ == nullptr) {
    const grpc_arg* channel_arg =
        grpc_channel_args_find(resolver_result.args, GRPC_ARG_LB_POLICY_NAME);
    lb_policy_name_.reset(gpr_strdup(grpc_channel_arg_get_string(channel_arg)));
  }
  // Special case: If at least one balancer address is present, we use
  // the grpclb policy, regardless of what the resolver has returned.
  bool found_balancer_address = false;
  for (size_t i = 0; i < resolver_result.addresses.size(); ++i) {
    const ServerAddress& address = resolver_result.addresses[i];
    if (address.IsBalancer()) {
      found_balancer_address = true;
      break;
    }
  }
  if (found_balancer_address) {
    if (lb_policy_name_ != nullptr &&
        strcmp(lb_policy_name_.get(), "grpclb") != 0) {
      gpr_log(GPR_INFO,
              "resolver requested LB policy %s but provided at least one "
              "balancer address -- forcing use of grpclb LB policy",
              lb_policy_name_.get());
    }
    lb_policy_name_.reset(gpr_strdup("grpclb"));
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (lb_policy_name_ == nullptr) {
    lb_policy_name_.reset(gpr_strdup("pick_first"));
  }
}

namespace {

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message, as per:
//   https://developers.google.com/protocol-buffers/docs/proto3#json
bool ParseDuration(grpc_json* field, grpc_millis* duration) {
  if (field->type != GRPC_JSON_STRING) return false;
  size_t len = strlen(field->value);
  if (field->value[len - 1] != 's') return false;
  UniquePtr<char> buf(gpr_strdup(field->value));
  *(buf.get() + len - 1) = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf.get(), '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      return false;
    }
    int num_digits = static_cast<int>(strlen(decimal_point + 1));
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds =
      decimal_point == buf.get() ? 0 : gpr_parse_nonnegative_int(buf.get());
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

UniquePtr<ClientChannelMethodParsedObject::RetryPolicy> ParseRetryPolicy(
    grpc_json* field, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  auto retry_policy =
      MakeUnique<ClientChannelMethodParsedObject::RetryPolicy>();
  if (field->type != GRPC_JSON_OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryPolicy error:should be of type object");
    return nullptr;
  }
  InlinedVector<grpc_error*, 4> error_list;
  for (grpc_json* sub_field = field->child; sub_field != nullptr;
       sub_field = sub_field->next) {
    if (sub_field->key == nullptr) continue;
    if (strcmp(sub_field->key, "maxAttempts") == 0) {
      if (retry_policy->max_attempts != 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxAttempts error:Duplicate entry"));
        continue;
      }  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxAttempts error:should be of type number"));
        continue;
      }
      retry_policy->max_attempts = gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->max_attempts <= 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxAttempts error:should be at least 2"));
        continue;
      }
      if (retry_policy->max_attempts > MAX_MAX_RETRY_ATTEMPTS) {
        gpr_log(GPR_ERROR,
                "service config: clamped retryPolicy.maxAttempts at %d",
                MAX_MAX_RETRY_ATTEMPTS);
        retry_policy->max_attempts = MAX_MAX_RETRY_ATTEMPTS;
      }
    } else if (strcmp(sub_field->key, "initialBackoff") == 0) {
      if (retry_policy->initial_backoff > 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:initialBackoff error:Duplicate entry"));
        continue;
      }
      if (!ParseDuration(sub_field, &retry_policy->initial_backoff)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:initialBackoff error:Failed to parse"));
        continue;
      }
      if (retry_policy->initial_backoff == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:initialBackoff error:must be greater than 0"));
      }
    } else if (strcmp(sub_field->key, "maxBackoff") == 0) {
      if (retry_policy->max_backoff > 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxBackoff error:Duplicate entry"));
        continue;
      }
      if (!ParseDuration(sub_field, &retry_policy->max_backoff)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxBackoff error:failed to parse"));
        continue;
      }
      if (retry_policy->max_backoff == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxBackoff error:should be greater than 0"));
      }
    } else if (strcmp(sub_field->key, "backoffMultiplier") == 0) {
      if (retry_policy->backoff_multiplier != 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:Duplicate entry"));
        continue;
      }
      if (sub_field->type != GRPC_JSON_NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:should be of type number"));
        continue;
      }
      if (sscanf(sub_field->value, "%f", &retry_policy->backoff_multiplier) !=
          1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:failed to parse"));
        continue;
      }
      if (retry_policy->backoff_multiplier <= 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:should be greater than 0"));
      }
    } else if (strcmp(sub_field->key, "retryableStatusCodes") == 0) {
      if (!retry_policy->retryable_status_codes.Empty()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryableStatusCodes error:Duplicate entry"));
        continue;
      }
      if (sub_field->type != GRPC_JSON_ARRAY) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryableStatusCodes error:should be of type array"));
        continue;
      }
      for (grpc_json* element = sub_field->child; element != nullptr;
           element = element->next) {
        if (element->type != GRPC_JSON_STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:retryableStatusCodes error:status codes should be of type "
              "string"));
          continue;
        }
        grpc_status_code status;
        if (!grpc_status_code_from_string(element->value, &status)) {
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
  *error = ServiceConfig::CreateErrorFromVector("retryPolicy", &error_list);
  return *error == GRPC_ERROR_NONE ? std::move(retry_policy) : nullptr;
}

}  // namespace

UniquePtr<ServiceConfig::ParsedConfig>
ClientChannelServiceConfigParser::ParseGlobalParams(const grpc_json* json,
                                                    grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  InlinedVector<grpc_error*, 4> error_list;
  RefCountedPtr<ParsedLoadBalancingConfig> parsed_lb_config;
  UniquePtr<char> lb_policy_name;
  Optional<ClientChannelGlobalParsedObject::RetryThrottling> retry_throttling;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) {
      continue;  // Not the LB config global parameter
    }
    // Parsed Load balancing config
    if (strcmp(field->key, "loadBalancingConfig") == 0) {
      if (parsed_lb_config != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:loadBalancingConfig error:Duplicate entry"));
      } else {
        grpc_error* parse_error = GRPC_ERROR_NONE;
        parsed_lb_config =
            LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(field,
                                                                  &parse_error);
        if (parsed_lb_config == nullptr) {
          error_list.push_back(parse_error);
        }
      }
    }
    // Parse deprecated loadBalancingPolicy
    if (strcmp(field->key, "loadBalancingPolicy") == 0) {
      if (lb_policy_name != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:loadBalancingPolicy error:Duplicate entry"));
        continue;
      } else if (field->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:loadBalancingPolicy error:type should be string"));
        continue;
      }
      lb_policy_name.reset(gpr_strdup(field->value));
      char* lb_policy = lb_policy_name.get();
      if (lb_policy != nullptr) {
        for (size_t i = 0; i < strlen(lb_policy); ++i) {
          lb_policy[i] = tolower(lb_policy[i]);
        }
      }
      if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(lb_policy)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:loadBalancingPolicy error:Unknown lb policy"));
      } else {
        grpc_error* parsing_error =
            LoadBalancingPolicyRegistry::CanCreateLoadBalancingPolicy(
                lb_policy);
        if (parsing_error != GRPC_ERROR_NONE) {
          error_list.push_back(parsing_error);
        }
      }
    }
    // Parse retry throttling
    if (strcmp(field->key, "retryThrottling") == 0) {
      if (field->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryThrottling error:Type should be object"));
      } else if (retry_throttling.has_value()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryThrottling error:Duplicate entry"));
      } else {
        Optional<int> max_milli_tokens(false, 0);
        Optional<int> milli_token_ratio(false, 0);
        for (grpc_json* sub_field = field->child; sub_field != nullptr;
             sub_field = sub_field->next) {
          if (sub_field->key == nullptr) continue;
          if (strcmp(sub_field->key, "maxTokens") == 0) {
            if (max_milli_tokens.has_value()) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "field:retryThrottling field:maxTokens error:Duplicate "
                  "entry"));
            } else if (sub_field->type != GRPC_JSON_NUMBER) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "field:retryThrottling field:maxTokens error:Type should be "
                  "number"));
            } else {
              max_milli_tokens.set(gpr_parse_nonnegative_int(sub_field->value) *
                                   1000);
              if (max_milli_tokens.value() <= 0) {
                error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "field:retryThrottling field:maxTokens error:should be "
                    "greater than zero"));
              }
            }
          } else if (strcmp(sub_field->key, "tokenRatio") == 0) {
            if (milli_token_ratio.has_value()) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "field:retryThrottling field:tokenRatio error:Duplicate "
                  "entry"));
            } else if (sub_field->type != GRPC_JSON_NUMBER) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "field:retryThrottling field:tokenRatio error:type should be "
                  "number"));
            } else {
              // We support up to 3 decimal digits.
              size_t whole_len = strlen(sub_field->value);
              uint32_t multiplier = 1;
              uint32_t decimal_value = 0;
              const char* decimal_point = strchr(sub_field->value, '.');
              if (decimal_point != nullptr) {
                whole_len =
                    static_cast<size_t>(decimal_point - sub_field->value);
                multiplier = 1000;
                size_t decimal_len = strlen(decimal_point + 1);
                if (decimal_len > 3) decimal_len = 3;
                if (!gpr_parse_bytes_to_uint32(decimal_point + 1, decimal_len,
                                               &decimal_value)) {
                  error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "field:retryThrottling field:tokenRatio error:Failed "
                      "parsing"));
                  continue;
                }
                uint32_t decimal_multiplier = 1;
                for (size_t i = 0; i < (3 - decimal_len); ++i) {
                  decimal_multiplier *= 10;
                }
                decimal_value *= decimal_multiplier;
              }
              uint32_t whole_value;
              if (!gpr_parse_bytes_to_uint32(sub_field->value, whole_len,
                                             &whole_value)) {
                error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "field:retryThrottling field:tokenRatio error:Failed "
                    "parsing"));
                continue;
              }
              milli_token_ratio.set(
                  static_cast<int>((whole_value * multiplier) + decimal_value));
              if (milli_token_ratio.value() <= 0) {
                error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "field:retryThrottling field:tokenRatio error:value should "
                    "be greater than 0"));
              }
            }
          }
        }
        if (!max_milli_tokens.has_value()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:retryThrottling field:maxTokens error:Not found"));
        }
        if (!milli_token_ratio.has_value()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:retryThrottling field:tokenRatio error:Not found"));
        }
        if (error_list.size() == 0) {
          ClientChannelGlobalParsedObject::RetryThrottling data;
          data.max_milli_tokens = max_milli_tokens.value();
          data.milli_token_ratio = milli_token_ratio.value();
          retry_throttling.set(data);
        }
      }
    }
  }
  *error = ServiceConfig::CreateErrorFromVector("Client channel global parser",
                                                &error_list);
  if (*error == GRPC_ERROR_NONE) {
    return UniquePtr<ServiceConfig::ParsedConfig>(
        New<ClientChannelGlobalParsedObject>(std::move(parsed_lb_config),
                                             std::move(lb_policy_name),
                                             retry_throttling));
  }
  return nullptr;
}

UniquePtr<ServiceConfig::ParsedConfig>
ClientChannelServiceConfigParser::ParsePerMethodParams(const grpc_json* json,
                                                       grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  InlinedVector<grpc_error*, 4> error_list;
  Optional<bool> wait_for_ready;
  grpc_millis timeout = 0;
  UniquePtr<ClientChannelMethodParsedObject::RetryPolicy> retry_policy;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready.has_value()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:waitForReady error:Duplicate entry"));
        continue;
      }
      if (field->type == GRPC_JSON_TRUE) {
        wait_for_ready.set(true);
      } else if (field->type == GRPC_JSON_FALSE) {
        wait_for_ready.set(false);
      } else {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:waitForReady error:Type should be true/false"));
      }
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout > 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:timeout error:Duplicate entry"));
        continue;
      }
      if (!ParseDuration(field, &timeout)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:timeout error:Failed parsing"));
      };
    } else if (strcmp(field->key, "retryPolicy") == 0) {
      if (retry_policy != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retryPolicy error:Duplicate entry"));
        continue;
      }
      grpc_error* error = GRPC_ERROR_NONE;
      retry_policy = ParseRetryPolicy(field, &error);
      if (retry_policy == nullptr) {
        error_list.push_back(error);
      }
    }
  }
  *error = ServiceConfig::CreateErrorFromVector("Client channel parser",
                                                &error_list);
  if (*error == GRPC_ERROR_NONE) {
    return UniquePtr<ServiceConfig::ParsedConfig>(
        New<ClientChannelMethodParsedObject>(timeout, wait_for_ready,
                                             std::move(retry_policy)));
  }
  return nullptr;
}

}  // namespace internal
}  // namespace grpc_core
