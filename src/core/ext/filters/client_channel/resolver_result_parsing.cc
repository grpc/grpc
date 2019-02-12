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
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/uri/uri_parser.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

ProcessedResolverResult::ProcessedResolverResult(
    const grpc_channel_args& resolver_result, bool parse_retry) {
  ProcessServiceConfig(resolver_result, parse_retry);
  // If no LB config was found above, just find the LB policy name then.
  if (lb_policy_name_ == nullptr) ProcessLbPolicyName(resolver_result);
}

void ProcessedResolverResult::ProcessServiceConfig(
    const grpc_channel_args& resolver_result, bool parse_retry) {
  const grpc_arg* channel_arg =
      grpc_channel_args_find(&resolver_result, GRPC_ARG_SERVICE_CONFIG);
  const char* service_config_json = grpc_channel_arg_get_string(channel_arg);
  if (service_config_json != nullptr) {
    service_config_json_.reset(gpr_strdup(service_config_json));
    service_config_ = grpc_core::ServiceConfig::Create(service_config_json);
    if (service_config_ != nullptr) {
      if (parse_retry) {
        channel_arg =
            grpc_channel_args_find(&resolver_result, GRPC_ARG_SERVER_URI);
        const char* server_uri = grpc_channel_arg_get_string(channel_arg);
        GPR_ASSERT(server_uri != nullptr);
        grpc_uri* uri = grpc_uri_parse(server_uri, true);
        GPR_ASSERT(uri->path[0] != '\0');
        server_name_ = uri->path[0] == '/' ? uri->path + 1 : uri->path;
        service_config_->ParseGlobalParams(ParseServiceConfig, this);
        grpc_uri_destroy(uri);
      } else {
        service_config_->ParseGlobalParams(ParseServiceConfig, this);
      }
      method_params_table_ = service_config_->CreateMethodConfigTable(
          ClientChannelMethodParams::CreateFromJson);
    }
  }
}

void ProcessedResolverResult::ProcessLbPolicyName(
    const grpc_channel_args& resolver_result) {
  // Prefer the LB policy name found in the service config. Note that this is
  // checking the deprecated loadBalancingPolicy field, rather than the new
  // loadBalancingConfig field.
  if (service_config_ != nullptr) {
    lb_policy_name_.reset(
        gpr_strdup(service_config_->GetLoadBalancingPolicyName()));
    // Convert to lower-case.
    if (lb_policy_name_ != nullptr) {
      char* lb_policy_name = lb_policy_name_.get();
      for (size_t i = 0; i < strlen(lb_policy_name); ++i) {
        lb_policy_name[i] = tolower(lb_policy_name[i]);
      }
    }
  }
  // Otherwise, find the LB policy name set by the client API.
  if (lb_policy_name_ == nullptr) {
    const grpc_arg* channel_arg =
        grpc_channel_args_find(&resolver_result, GRPC_ARG_LB_POLICY_NAME);
    lb_policy_name_.reset(gpr_strdup(grpc_channel_arg_get_string(channel_arg)));
  }
  // Special case: If at least one balancer address is present, we use
  // the grpclb policy, regardless of what the resolver has returned.
  const ServerAddressList* addresses =
      FindServerAddressListChannelArg(&resolver_result);
  if (addresses != nullptr) {
    bool found_balancer_address = false;
    for (size_t i = 0; i < addresses->size(); ++i) {
      const ServerAddress& address = (*addresses)[i];
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
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (lb_policy_name_ == nullptr) {
    lb_policy_name_.reset(gpr_strdup("pick_first"));
  }
}

void ProcessedResolverResult::ParseServiceConfig(
    const grpc_json* field, ProcessedResolverResult* parsing_state) {
  parsing_state->ParseLbConfigFromServiceConfig(field);
  if (parsing_state->server_name_ != nullptr) {
    parsing_state->ParseRetryThrottleParamsFromServiceConfig(field);
  }
}

void ProcessedResolverResult::ParseLbConfigFromServiceConfig(
    const grpc_json* field) {
  if (lb_policy_config_ != nullptr) return;  // Already found.
  if (field->key == nullptr || strcmp(field->key, "loadBalancingConfig") != 0) {
    return;  // Not the LB config global parameter.
  }
  const grpc_json* policy =
      LoadBalancingPolicy::ParseLoadBalancingConfig(field);
  if (policy != nullptr) {
    lb_policy_name_.reset(gpr_strdup(policy->key));
    lb_policy_config_ = policy->child;
  }
}

void ProcessedResolverResult::ParseRetryThrottleParamsFromServiceConfig(
    const grpc_json* field) {
  if (strcmp(field->key, "retryThrottling") == 0) {
    if (retry_throttle_data_ != nullptr) return;  // Duplicate.
    if (field->type != GRPC_JSON_OBJECT) return;
    int max_milli_tokens = 0;
    int milli_token_ratio = 0;
    for (grpc_json* sub_field = field->child; sub_field != nullptr;
         sub_field = sub_field->next) {
      if (sub_field->key == nullptr) return;
      if (strcmp(sub_field->key, "maxTokens") == 0) {
        if (max_milli_tokens != 0) return;  // Duplicate.
        if (sub_field->type != GRPC_JSON_NUMBER) return;
        max_milli_tokens = gpr_parse_nonnegative_int(sub_field->value);
        if (max_milli_tokens == -1) return;
        max_milli_tokens *= 1000;
      } else if (strcmp(sub_field->key, "tokenRatio") == 0) {
        if (milli_token_ratio != 0) return;  // Duplicate.
        if (sub_field->type != GRPC_JSON_NUMBER) return;
        // We support up to 3 decimal digits.
        size_t whole_len = strlen(sub_field->value);
        uint32_t multiplier = 1;
        uint32_t decimal_value = 0;
        const char* decimal_point = strchr(sub_field->value, '.');
        if (decimal_point != nullptr) {
          whole_len = static_cast<size_t>(decimal_point - sub_field->value);
          multiplier = 1000;
          size_t decimal_len = strlen(decimal_point + 1);
          if (decimal_len > 3) decimal_len = 3;
          if (!gpr_parse_bytes_to_uint32(decimal_point + 1, decimal_len,
                                         &decimal_value)) {
            return;
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
          return;
        }
        milli_token_ratio =
            static_cast<int>((whole_value * multiplier) + decimal_value);
        if (milli_token_ratio <= 0) return;
      }
    }
    retry_throttle_data_ =
        grpc_core::internal::ServerRetryThrottleMap::GetDataForServer(
            server_name_, max_milli_tokens, milli_token_ratio);
  }
}

namespace {

bool ParseWaitForReady(
    grpc_json* field, ClientChannelMethodParams::WaitForReady* wait_for_ready) {
  if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
    return false;
  }
  *wait_for_ready = field->type == GRPC_JSON_TRUE
                        ? ClientChannelMethodParams::WAIT_FOR_READY_TRUE
                        : ClientChannelMethodParams::WAIT_FOR_READY_FALSE;
  return true;
}

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

UniquePtr<ClientChannelMethodParams::RetryPolicy> ParseRetryPolicy(
    grpc_json* field) {
  auto retry_policy = MakeUnique<ClientChannelMethodParams::RetryPolicy>();
  if (field->type != GRPC_JSON_OBJECT) return nullptr;
  for (grpc_json* sub_field = field->child; sub_field != nullptr;
       sub_field = sub_field->next) {
    if (sub_field->key == nullptr) return nullptr;
    if (strcmp(sub_field->key, "maxAttempts") == 0) {
      if (retry_policy->max_attempts != 0) return nullptr;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return nullptr;
      retry_policy->max_attempts = gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->max_attempts <= 1) return nullptr;
      if (retry_policy->max_attempts > MAX_MAX_RETRY_ATTEMPTS) {
        gpr_log(GPR_ERROR,
                "service config: clamped retryPolicy.maxAttempts at %d",
                MAX_MAX_RETRY_ATTEMPTS);
        retry_policy->max_attempts = MAX_MAX_RETRY_ATTEMPTS;
      }
    } else if (strcmp(sub_field->key, "initialBackoff") == 0) {
      if (retry_policy->initial_backoff > 0) return nullptr;  // Duplicate.
      if (!ParseDuration(sub_field, &retry_policy->initial_backoff)) {
        return nullptr;
      }
      if (retry_policy->initial_backoff == 0) return nullptr;
    } else if (strcmp(sub_field->key, "maxBackoff") == 0) {
      if (retry_policy->max_backoff > 0) return nullptr;  // Duplicate.
      if (!ParseDuration(sub_field, &retry_policy->max_backoff)) {
        return nullptr;
      }
      if (retry_policy->max_backoff == 0) return nullptr;
    } else if (strcmp(sub_field->key, "backoffMultiplier") == 0) {
      if (retry_policy->backoff_multiplier != 0) return nullptr;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return nullptr;
      if (sscanf(sub_field->value, "%f", &retry_policy->backoff_multiplier) !=
          1) {
        return nullptr;
      }
      if (retry_policy->backoff_multiplier <= 0) return nullptr;
    } else if (strcmp(sub_field->key, "retryableStatusCodes") == 0) {
      if (!retry_policy->retryable_status_codes.Empty()) {
        return nullptr;  // Duplicate.
      }
      if (sub_field->type != GRPC_JSON_ARRAY) return nullptr;
      for (grpc_json* element = sub_field->child; element != nullptr;
           element = element->next) {
        if (element->type != GRPC_JSON_STRING) return nullptr;
        grpc_status_code status;
        if (!grpc_status_code_from_string(element->value, &status)) {
          return nullptr;
        }
        retry_policy->retryable_status_codes.Add(status);
      }
      if (retry_policy->retryable_status_codes.Empty()) return nullptr;
    }
  }
  // Make sure required fields are set.
  if (retry_policy->max_attempts == 0 || retry_policy->initial_backoff == 0 ||
      retry_policy->max_backoff == 0 || retry_policy->backoff_multiplier == 0 ||
      retry_policy->retryable_status_codes.Empty()) {
    return nullptr;
  }
  return retry_policy;
}

}  // namespace

RefCountedPtr<ClientChannelMethodParams>
ClientChannelMethodParams::CreateFromJson(const grpc_json* json) {
  RefCountedPtr<ClientChannelMethodParams> method_params =
      MakeRefCounted<ClientChannelMethodParams>();
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (method_params->wait_for_ready_ != WAIT_FOR_READY_UNSET) {
        return nullptr;  // Duplicate.
      }
      if (!ParseWaitForReady(field, &method_params->wait_for_ready_)) {
        return nullptr;
      }
    } else if (strcmp(field->key, "timeout") == 0) {
      if (method_params->timeout_ > 0) return nullptr;  // Duplicate.
      if (!ParseDuration(field, &method_params->timeout_)) return nullptr;
    } else if (strcmp(field->key, "retryPolicy") == 0) {
      if (method_params->retry_policy_ != nullptr) {
        return nullptr;  // Duplicate.
      }
      method_params->retry_policy_ = ParseRetryPolicy(field);
      if (method_params->retry_policy_ == nullptr) return nullptr;
    }
  }
  return method_params;
}

}  // namespace internal
}  // namespace grpc_core
