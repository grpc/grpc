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

#include "src/core/ext/filters/fault_injection/service_config_parser.h"

#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/fault_injection/fault_injection_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/json/json_util.h"

namespace grpc_core {

namespace {

size_t g_fault_injection_parser_index;

void ParseFaultInjectionPolicy(const Json::Array* policies_json_array,
                               std::vector<FaultInjectionPolicy>* policies,
                               std::vector<grpc_error*>* error_list) {
  for (auto json : *policies_json_array) {
    FaultInjectionPolicy fault_injection_policy;
    const Json::Object json_object = json.object_value();
    // Parse abort_code
    std::string abort_code_string;
    if (ParseJsonObjectField(json_object, "abortCode", &abort_code_string,
                             error_list, false)) {
      if (!grpc_status_code_from_string(abort_code_string.c_str(),
                                        &(fault_injection_policy.abort_code))) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:abortCode error:failed to parse status code"));
      }
    }
    // Parse abort_message
    if (!ParseJsonObjectField(json_object, "abortMessage",
                              &fault_injection_policy.abort_message, error_list,
                              false)) {
      fault_injection_policy.abort_message = "Fault injected";
    }
    // Parse abort_code_header
    ParseJsonObjectField(json_object, "abortCodeHeader",
                         &fault_injection_policy.abort_code_header, error_list,
                         false);
    // Parse abort_percentage_header
    ParseJsonObjectField(json_object, "abortPercentageHeader",
                         &fault_injection_policy.abort_percentage_header,
                         error_list, false);
    // Parse abort_percentage_numerator
    ParseJsonObjectField(json_object, "abortPercentageNumerator",
                         &fault_injection_policy.abort_percentage_numerator,
                         error_list, false);
    // Parse abort_percentage_denominator
    if (ParseJsonObjectField(
            json_object, "abortPercentageDenominator",
            &fault_injection_policy.abort_percentage_denominator, error_list,
            false)) {
      if (fault_injection_policy.abort_percentage_denominator != 100 &&
          fault_injection_policy.abort_percentage_denominator != 10000 &&
          fault_injection_policy.abort_percentage_denominator != 1000000) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:abortPercentageDenominator error:Denominator can only be "
            "one of "
            "100, 10000, 1000000"));
      }
    }
    // Parse delay
    ParseJsonObjectFieldAsDuration(
        json_object, "delay", &fault_injection_policy.delay, error_list, false);
    // Parse delay_header
    ParseJsonObjectField(json_object, "delayHeader",
                         &fault_injection_policy.delay_header, error_list,
                         false);
    // Parse delay_percentage_header
    ParseJsonObjectField(json_object, "delayPercentageHeader",
                         &fault_injection_policy.delay_percentage_header,
                         error_list, false);
    // Parse delay_percentage_numerator
    ParseJsonObjectField(json_object, "delayPercentageNumerator",
                         &fault_injection_policy.delay_percentage_numerator,
                         error_list, false);
    // Parse delay_percentage_denominator
    if (ParseJsonObjectField(
            json_object, "delayPercentageDenominator",
            &fault_injection_policy.delay_percentage_denominator, error_list,
            false)) {
      if (fault_injection_policy.delay_percentage_denominator != 100 &&
          fault_injection_policy.delay_percentage_denominator != 10000 &&
          fault_injection_policy.delay_percentage_denominator != 1000000) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:delayPercentageDenominator error:Denominator can only be "
            "one of "
            "100, 10000, 1000000"));
      }
    }
    // Parse max_faults
    if (ParseJsonObjectField(json_object, "maxFaults",
                             &fault_injection_policy.max_faults, error_list,
                             false)) {
      if (fault_injection_policy.max_faults < 0) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxFaults error:should be zero or positive"));
      }
    }
    policies->push_back(std::move(fault_injection_policy));
  }
}

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
FaultInjectionServiceConfigParser::ParsePerMethodParams(
    const grpc_channel_args* args, const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  // Only parse fault injection policy if the following channel arg is present.
  if (!grpc_channel_args_find_bool(
          args, GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG, false)) {
    return nullptr;
  }
  // Parse fault injection policy from given Json
  std::vector<FaultInjectionPolicy> fault_injection_policies;
  std::vector<grpc_error*> error_list;
  const Json::Array* policies_json_array;
  if (ParseJsonObjectField(json.object_value(), "faultInjectionPolicy",
                           &policies_json_array, &error_list)) {
    ParseFaultInjectionPolicy(policies_json_array, &fault_injection_policies,
                              &error_list);
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("Fault injection parser", &error_list);
  if (*error != GRPC_ERROR_NONE || fault_injection_policies.empty()) {
    return nullptr;
  }
  return absl::make_unique<FaultInjectionMethodParsedConfig>(
      std::move(fault_injection_policies));
}

void FaultInjectionServiceConfigParser::Register() {
  g_fault_injection_parser_index = ServiceConfigParser::RegisterParser(
      absl::make_unique<FaultInjectionServiceConfigParser>());
}

size_t FaultInjectionServiceConfigParser::ParserIndex() {
  return g_fault_injection_parser_index;
}

}  // namespace grpc_core
