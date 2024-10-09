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

#include "src/core/client_channel/retry_service_config.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/util/json/json_channel_args.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

//
// RetryGlobalConfig
//

const JsonLoaderInterface* RetryGlobalConfig::JsonLoader(const JsonArgs&) {
  // Note: Both fields require custom processing, so they're handled in
  // JsonPostLoad() instead.
  static const auto* loader = JsonObjectLoader<RetryGlobalConfig>().Finish();
  return loader;
}

void RetryGlobalConfig::JsonPostLoad(const Json& json, const JsonArgs& args,
                                     ValidationErrors* errors) {
  // Parse maxTokens.
  auto max_tokens =
      LoadJsonObjectField<uint32_t>(json.object(), args, "maxTokens", errors);
  if (max_tokens.has_value()) {
    ValidationErrors::ScopedField field(errors, ".maxTokens");
    if (*max_tokens == 0) {
      errors->AddError("must be greater than 0");
    } else {
      // Multiply by 1000 to represent as milli-tokens.
      max_milli_tokens_ = static_cast<uintptr_t>(*max_tokens) * 1000;
    }
  }
  // Parse tokenRatio.
  ValidationErrors::ScopedField field(errors, ".tokenRatio");
  auto it = json.object().find("tokenRatio");
  if (it == json.object().end()) {
    errors->AddError("field not present");
    return;
  }
  if (it->second.type() != Json::Type::kNumber &&
      it->second.type() != Json::Type::kString) {
    errors->AddError("is not a number");
    return;
  }
  absl::string_view buf = it->second.string();
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
// RetryMethodConfig
//

const JsonLoaderInterface* RetryMethodConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<RetryMethodConfig>()
          // Note: The "retryableStatusCodes" field requires custom parsing,
          // so it's handled in JsonPostLoad() instead.
          .Field("maxAttempts", &RetryMethodConfig::max_attempts_)
          .Field("initialBackoff", &RetryMethodConfig::initial_backoff_)
          .Field("maxBackoff", &RetryMethodConfig::max_backoff_)
          .Field("backoffMultiplier", &RetryMethodConfig::backoff_multiplier_)
          .OptionalField("perAttemptRecvTimeout",
                         &RetryMethodConfig::per_attempt_recv_timeout_,
                         GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING)
          .Finish();
  return loader;
}

void RetryMethodConfig::JsonPostLoad(const Json& json, const JsonArgs& args,
                                     ValidationErrors* errors) {
  // Validate maxAttempts.
  {
    ValidationErrors::ScopedField field(errors, ".maxAttempts");
    if (!errors->FieldHasErrors()) {
      if (max_attempts_ <= 1) {
        errors->AddError("must be at least 2");
      } else if (max_attempts_ > MAX_MAX_RETRY_ATTEMPTS) {
        LOG(ERROR) << "service config: clamped retryPolicy.maxAttempts at "
                   << MAX_MAX_RETRY_ATTEMPTS;
        max_attempts_ = MAX_MAX_RETRY_ATTEMPTS;
      }
    }
  }
  // Validate initialBackoff.
  {
    ValidationErrors::ScopedField field(errors, ".initialBackoff");
    if (!errors->FieldHasErrors() && initial_backoff_ == Duration::Zero()) {
      errors->AddError("must be greater than 0");
    }
  }
  // Validate maxBackoff.
  {
    ValidationErrors::ScopedField field(errors, ".maxBackoff");
    if (!errors->FieldHasErrors() && max_backoff_ == Duration::Zero()) {
      errors->AddError("must be greater than 0");
    }
  }
  // Validate backoffMultiplier.
  {
    ValidationErrors::ScopedField field(errors, ".backoffMultiplier");
    if (!errors->FieldHasErrors() && backoff_multiplier_ <= 0) {
      errors->AddError("must be greater than 0");
    }
  }
  // Parse retryableStatusCodes.
  auto status_code_list = LoadJsonObjectField<std::vector<std::string>>(
      json.object(), args, "retryableStatusCodes", errors,
      /*required=*/false);
  if (status_code_list.has_value()) {
    for (size_t i = 0; i < status_code_list->size(); ++i) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".retryableStatusCodes[", i, "]"));
      grpc_status_code status;
      if (!grpc_status_code_from_string((*status_code_list)[i].c_str(),
                                        &status)) {
        errors->AddError("failed to parse status code");
      } else {
        retryable_status_codes_.Add(status);
      }
    }
  }
  // Validate perAttemptRecvTimeout.
  if (args.IsEnabled(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING)) {
    if (per_attempt_recv_timeout_.has_value()) {
      ValidationErrors::ScopedField field(errors, ".perAttemptRecvTimeout");
      // TODO(roth): As part of implementing hedging, relax this check such
      // that we allow a value of 0 if a hedging policy is specified.
      if (!errors->FieldHasErrors() &&
          *per_attempt_recv_timeout_ == Duration::Zero()) {
        errors->AddError("must be greater than 0");
      }
    } else if (retryable_status_codes_.Empty()) {
      // If perAttemptRecvTimeout not present, retryableStatusCodes must be
      // non-empty.
      ValidationErrors::ScopedField field(errors, ".retryableStatusCodes");
      if (!errors->FieldHasErrors()) {
        errors->AddError(
            "must be non-empty if perAttemptRecvTimeout not present");
      }
    }
  } else if (retryable_status_codes_.Empty()) {
    // Hedging not enabled, so the error message for
    // retryableStatusCodes unset should be different.
    ValidationErrors::ScopedField field(errors, ".retryableStatusCodes");
    if (!errors->FieldHasErrors()) {
      errors->AddError("must be non-empty");
    }
  }
}

//
// RetryServiceConfigParser
//

size_t RetryServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

void RetryServiceConfigParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<RetryServiceConfigParser>());
}

namespace {

struct GlobalConfig {
  std::unique_ptr<RetryGlobalConfig> retry_throttling;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<GlobalConfig>()
            .OptionalField("retryThrottling", &GlobalConfig::retry_throttling)
            .Finish();
    return loader;
  }
};

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
RetryServiceConfigParser::ParseGlobalParams(const ChannelArgs& /*args*/,
                                            const Json& json,
                                            ValidationErrors* errors) {
  auto global_params = LoadFromJson<GlobalConfig>(json, JsonArgs(), errors);
  return std::move(global_params.retry_throttling);
}

namespace {

struct MethodConfig {
  std::unique_ptr<RetryMethodConfig> retry_policy;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<MethodConfig>()
            .OptionalField("retryPolicy", &MethodConfig::retry_policy)
            .Finish();
    return loader;
  }
};

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
RetryServiceConfigParser::ParsePerMethodParams(const ChannelArgs& args,
                                               const Json& json,
                                               ValidationErrors* errors) {
  auto method_params =
      LoadFromJson<MethodConfig>(json, JsonChannelArgs(args), errors);
  return std::move(method_params.retry_policy);
}

}  // namespace internal
}  // namespace grpc_core
