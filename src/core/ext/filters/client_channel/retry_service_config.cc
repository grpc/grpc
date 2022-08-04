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

#include "src/core/ext/filters/client_channel/retry_service_config.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json_util.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

size_t RetryServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

void RetryServiceConfigParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      absl::make_unique<RetryServiceConfigParser>());
}

namespace {

grpc_error_handle ParseRetryThrottling(const Json& json,
                                       intptr_t* max_milli_tokens,
                                       intptr_t* milli_token_ratio) {
  if (json.type() != Json::Type::OBJECT) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryThrottling error:Type should be object");
  }
  std::vector<grpc_error_handle> error_list;
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
    *max_milli_tokens =
        gpr_parse_nonnegative_int(it->second.string_value().c_str()) * 1000;
    if (*max_milli_tokens <= 0) {
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
        return GRPC_ERROR_CREATE_FROM_VECTOR("retryThrottling", &error_list);
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
      return GRPC_ERROR_CREATE_FROM_VECTOR("retryThrottling", &error_list);
    }
    *milli_token_ratio =
        static_cast<int>((whole_value * multiplier) + decimal_value);
    if (*milli_token_ratio <= 0) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryThrottling field:tokenRatio error:value should "
          "be greater than 0"));
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("retryThrottling", &error_list);
}

}  // namespace

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
RetryServiceConfigParser::ParseGlobalParams(const ChannelArgs& /*args*/,
                                            const Json& json) {
  auto it = json.object_value().find("retryThrottling");
  if (it == json.object_value().end()) return nullptr;
  intptr_t max_milli_tokens = 0;
  intptr_t milli_token_ratio = 0;
  grpc_error_handle error =
      ParseRetryThrottling(it->second, &max_milli_tokens, &milli_token_ratio);
  if (!GRPC_ERROR_IS_NONE(error)) {
    absl::Status status = absl::InvalidArgumentError(
        absl::StrCat("error parsing retry global parameters: ",
                     grpc_error_std_string(error)));
    GRPC_ERROR_UNREF(error);
    return status;
  }
  return absl::make_unique<RetryGlobalConfig>(max_milli_tokens,
                                              milli_token_ratio);
}

namespace {

grpc_error_handle ParseRetryPolicy(
    const ChannelArgs& args, const Json& json, int* max_attempts,
    Duration* initial_backoff, Duration* max_backoff, float* backoff_multiplier,
    StatusCodeSet* retryable_status_codes,
    absl::optional<Duration>* per_attempt_recv_timeout) {
  if (json.type() != Json::Type::OBJECT) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:retryPolicy error:should be of type object");
  }
  std::vector<grpc_error_handle> error_list;
  // Parse maxAttempts.
  auto it = json.object_value().find("maxAttempts");
  if (it == json.object_value().end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:maxAttempts error:required field missing"));
  } else {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:maxAttempts error:should be of type number"));
    } else {
      *max_attempts =
          gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (*max_attempts <= 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxAttempts error:should be at least 2"));
      } else if (*max_attempts > MAX_MAX_RETRY_ATTEMPTS) {
        gpr_log(GPR_ERROR,
                "service config: clamped retryPolicy.maxAttempts at %d",
                MAX_MAX_RETRY_ATTEMPTS);
        *max_attempts = MAX_MAX_RETRY_ATTEMPTS;
      }
    }
  }
  // Parse initialBackoff.
  if (ParseJsonObjectFieldAsDuration(json.object_value(), "initialBackoff",
                                     initial_backoff, &error_list) &&
      *initial_backoff == Duration::Zero()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:initialBackoff error:must be greater than 0"));
  }
  // Parse maxBackoff.
  if (ParseJsonObjectFieldAsDuration(json.object_value(), "maxBackoff",
                                     max_backoff, &error_list) &&
      *max_backoff == Duration::Zero()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:maxBackoff error:must be greater than 0"));
  }
  // Parse backoffMultiplier.
  it = json.object_value().find("backoffMultiplier");
  if (it == json.object_value().end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:backoffMultiplier error:required field missing"));
  } else {
    if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:backoffMultiplier error:should be of type number"));
    } else {
      if (sscanf(it->second.string_value().c_str(), "%f", backoff_multiplier) !=
          1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:failed to parse"));
      } else if (*backoff_multiplier <= 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:backoffMultiplier error:must be greater than 0"));
      }
    }
  }
  // Parse retryableStatusCodes.
  it = json.object_value().find("retryableStatusCodes");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryableStatusCodes error:must be of type array"));
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
        retryable_status_codes->Add(status);
      }
    }
  }
  // Parse perAttemptRecvTimeout.
  if (args.GetBool(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING).value_or(false)) {
    it = json.object_value().find("perAttemptRecvTimeout");
    if (it != json.object_value().end()) {
      Duration per_attempt_recv_timeout_value;
      if (!ParseDurationFromJson(it->second, &per_attempt_recv_timeout_value)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:perAttemptRecvTimeout error:type must be STRING of the "
            "form given by google.proto.Duration."));
      } else {
        *per_attempt_recv_timeout = per_attempt_recv_timeout_value;
        // TODO(roth): As part of implementing hedging, relax this check such
        // that we allow a value of 0 if a hedging policy is specified.
        if (per_attempt_recv_timeout_value == Duration::Zero()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:perAttemptRecvTimeout error:must be greater than 0"));
        }
      }
    } else if (retryable_status_codes->Empty()) {
      // If perAttemptRecvTimeout not present, retryableStatusCodes must be
      // non-empty.
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryableStatusCodes error:must be non-empty if "
          "perAttemptRecvTimeout not present"));
    }
  } else {
    // Hedging not enabled, so the error message for
    // retryableStatusCodes unset should be different.
    if (retryable_status_codes->Empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:retryableStatusCodes error:must be non-empty"));
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("retryPolicy", &error_list);
}

}  // namespace

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
RetryServiceConfigParser::ParsePerMethodParams(const ChannelArgs& args,
                                               const Json& json) {
  // Parse retry policy.
  auto it = json.object_value().find("retryPolicy");
  if (it == json.object_value().end()) return nullptr;
  int max_attempts = 0;
  Duration initial_backoff;
  Duration max_backoff;
  float backoff_multiplier = 0;
  StatusCodeSet retryable_status_codes;
  absl::optional<Duration> per_attempt_recv_timeout;
  grpc_error_handle error = ParseRetryPolicy(
      args, it->second, &max_attempts, &initial_backoff, &max_backoff,
      &backoff_multiplier, &retryable_status_codes, &per_attempt_recv_timeout);
  if (!GRPC_ERROR_IS_NONE(error)) {
    absl::Status status = absl::InvalidArgumentError(
        absl::StrCat("error parsing retry method parameters: ",
                     grpc_error_std_string(error)));
    GRPC_ERROR_UNREF(error);
    return status;
  }
  return absl::make_unique<RetryMethodConfig>(
      max_attempts, initial_backoff, max_backoff, backoff_multiplier,
      retryable_status_codes, per_attempt_recv_timeout);
}

}  // namespace internal
}  // namespace grpc_core
