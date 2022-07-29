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

//
// RetryGlobalConfig
//

const JsonLoaderInterface* RetryGlobalConfig::JsonLoader() {
  static const auto loader =
      JsonObjectLoader<RetryGlobalConfig>()
          // Note: The "tokenRatio" field requires custom parsing, so
          // it's handled in JsonPostLoad() instead.
          .Field("maxTokens", &RetryGlobalConfig::max_milli_tokens_)
          .Finish();
  return &loader;
}

void RetryGlobalConfig::JsonPostLoad(const Json& json, ErrorList* errors) {
  // Validate maxTokens.
  if (json.object_value().find("maxTokens") != json.object_value().end()) {
    ScopedField field(errors, ".maxTokens");
    if (max_milli_tokens_ == 0) errors->AddError("must be greater than 0");
    // Multiply by 1000 to represent as milli-tokens.
    max_milli_tokens_ *= 1000;
  }
  // Parse tokenRatio.
  ScopedField field(errors, ".tokenRatio");
  auto it = json.object_value().find("tokenRatio");
  if (it == json.object_value().end()) {
    errors->AddError("field not present");
    return;
  }
  if (it->second.type() != Json::Type::NUMBER &&
      it->second.type() != Json::Type::STRING) {
    errors->AddError("is not a number");
    return;
  }
  absl::string_view buf = it->second.string_value();
  uint32_t multiplier = 1;
  uint32_t decimal_value = 0;
  auto decimal_point = buf.find('.');
  if (decimal_point != absl::string_view::npos) {
    absl::string_view after_decimal = buf.substr(decimal_point + 1);
    buf = buf.substr(0, decimal_point);
    // We support up to 3 decimal digits.
    multiplier = 1000;
    if (after_decimal.length() > 3) after_decimal = after_decimal.substr(0, 3);
    // Parse decimal value.
    if (!absl::SimpleAtoi(after_decimal, &decimal_value)) {
      errors->AddError("could not parse as a number");
      return;
    }
    uint32_t decimal_multiplier = 1;
    for (size_t i = 0; i < (3 - after_decimal.length()); ++i) {
      decimal_multiplier *= 10;
    }
    decimal_value *= decimal_multiplier;
  }
  uint32_t whole_value;
  if (!absl::SimpleAtoi(buf, &whole_value)) {
    errors->AddError("could not parse as a number");
    return;
  }
  milli_token_ratio_ =
      static_cast<int>((whole_value * multiplier) + decimal_value);
  if (milli_token_ratio_ <= 0) {
    errors->AddError("must be greater than 0");
  }
}

//
// RetryServiceConfigParser
//

namespace {

struct GlobalConfig {
  absl::optional<RetryGlobalConfig> retry_throttling;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto loader =
        JsonObjectLoader<GlobalConfig>()
            .OptionalField("retryThrottling", &GlobalConfig::retry_throttling)
            .Finish();
    return &loader;
  }
};

}  // namespace

size_t RetryServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

void RetryServiceConfigParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      absl::make_unique<RetryServiceConfigParser>());
}

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
RetryServiceConfigParser::ParseGlobalParams(const ChannelArgs& /*args*/,
                                            const Json& json) {
  auto global_params = LoadFromJson<GlobalConfig>(json);
  if (!global_params.ok()) return global_params.status();
  // If the retryThrottling field was not present, no need to return any
  // parsed config.
  if (!global_params->retry_throttling.has_value()) return nullptr;
  return absl::make_unique<RetryGlobalConfig>(
      std::move(*global_params->retry_throttling));
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
