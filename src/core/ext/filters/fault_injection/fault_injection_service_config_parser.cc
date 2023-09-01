//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/fault_injection/fault_injection_service_config_parser.h"

#include <vector>

#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"

namespace grpc_core {

const JsonLoaderInterface*
FaultInjectionMethodParsedConfig::FaultInjectionPolicy::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<FaultInjectionPolicy>()
          .OptionalField("abortMessage", &FaultInjectionPolicy::abort_message)
          .OptionalField("abortCodeHeader",
                         &FaultInjectionPolicy::abort_code_header)
          .OptionalField("abortPercentageHeader",
                         &FaultInjectionPolicy::abort_percentage_header)
          .OptionalField("abortPercentageNumerator",
                         &FaultInjectionPolicy::abort_percentage_numerator)
          .OptionalField("abortPercentageDenominator",
                         &FaultInjectionPolicy::abort_percentage_denominator)
          .OptionalField("delay", &FaultInjectionPolicy::delay)
          .OptionalField("delayHeader", &FaultInjectionPolicy::delay_header)
          .OptionalField("delayPercentageHeader",
                         &FaultInjectionPolicy::delay_percentage_header)
          .OptionalField("delayPercentageNumerator",
                         &FaultInjectionPolicy::delay_percentage_numerator)
          .OptionalField("delayPercentageDenominator",
                         &FaultInjectionPolicy::delay_percentage_denominator)
          .OptionalField("maxFaults", &FaultInjectionPolicy::max_faults)
          .Finish();
  return loader;
}

void FaultInjectionMethodParsedConfig::FaultInjectionPolicy::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  // Parse abort_code.
  auto abort_code_string = LoadJsonObjectField<std::string>(
      json.object(), args, "abortCode", errors, /*required=*/false);
  if (abort_code_string.has_value() &&
      !grpc_status_code_from_string(abort_code_string->c_str(), &abort_code)) {
    ValidationErrors::ScopedField field(errors, ".abortCode");
    errors->AddError("failed to parse status code");
  }
  // Validate abort_percentage_denominator.
  if (abort_percentage_denominator != 100 &&
      abort_percentage_denominator != 10000 &&
      abort_percentage_denominator != 1000000) {
    ValidationErrors::ScopedField field(errors, ".abortPercentageDenominator");
    errors->AddError("must be one of 100, 10000, or 1000000");
  }
  // Validate delay_percentage_denominator.
  if (delay_percentage_denominator != 100 &&
      delay_percentage_denominator != 10000 &&
      delay_percentage_denominator != 1000000) {
    ValidationErrors::ScopedField field(errors, ".delayPercentageDenominator");
    errors->AddError("must be one of 100, 10000, or 1000000");
  }
}

const JsonLoaderInterface* FaultInjectionMethodParsedConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<FaultInjectionMethodParsedConfig>()
          .OptionalField(
              "faultInjectionPolicy",
              &FaultInjectionMethodParsedConfig::fault_injection_policies_)
          .Finish();
  return loader;
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
FaultInjectionServiceConfigParser::ParsePerMethodParams(
    const ChannelArgs& args, const Json& json, ValidationErrors* errors) {
  // Only parse fault injection policy if the following channel arg is present.
  if (!args.GetBool(GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG)
           .value_or(false)) {
    return nullptr;
  }
  // Parse fault injection policy from given Json
  return LoadFromJson<std::unique_ptr<FaultInjectionMethodParsedConfig>>(
      json, JsonArgs(), errors);
}

void FaultInjectionServiceConfigParser::Register(
    CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<FaultInjectionServiceConfigParser>());
}

size_t FaultInjectionServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

}  // namespace grpc_core
