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

// TODO(lidiz) this function is similar to json_util.h's yet it handles extra
// logic that fetches the field out of an Json::Object. Upon requested, we can
// update to use functions from json_util.h
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

std::vector<FaultInjectionPolicy> ParseFaultInjectionPolicy(
    const Json& policies_json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  std::vector<FaultInjectionPolicy> policies;
  std::vector<grpc_error*> error_list;
  if (policies_json.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:faultInjectionPolicy error:should be of type array");
    return policies;
  }
  for (auto& json : policies_json.array_value()) {
    FaultInjectionPolicy fault_injection_policy;
    // Parse abort_per_million
    fault_injection_policy.abort_per_million =
        ParsePerMillionField(json, "abortPerMillion", &error_list);
    // Parse abort_code
    auto it = json.object_value().find("abortCode");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:abortCode error:should be of type string"));
      } else if (!grpc_status_code_from_string(
                     it->second.string_value().c_str(),
                     &(fault_injection_policy.abort_code))) {
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
        fault_injection_policy.abort_message = it->second.string_value();
      }
    } else {
      fault_injection_policy.abort_message = "Fault injected";
    }
    // Parse abort_code_header
    fault_injection_policy.abort_code_header =
        ParseStringField(json, "abortCodeHeader", &error_list);
    // Parse abort_percentage_header
    fault_injection_policy.abort_percentage_header =
        ParseStringField(json, "abortPercentageHeader", &error_list);
    // Parse abort_percentage_denominator
    it = json.object_value().find("abortPercentageDenominator");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:abortPercentageDenominator error:should be of type number"));
      } else {
        fault_injection_policy.abort_percentage_denominator =
            gpr_parse_nonnegative_int(it->second.string_value().c_str());
        if (fault_injection_policy.abort_percentage_denominator != 100 &&
            fault_injection_policy.abort_percentage_denominator != 10000 &&
            fault_injection_policy.abort_percentage_denominator != 1000000) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:abortPercentageDenominator error:Denominator can only be "
              "one of "
              "100, 10000, 1000000"));
        }
      }
    }
    // Parse delay_per_million
    fault_injection_policy.delay_per_million =
        ParsePerMillionField(json, "delayPerMillion", &error_list);
    // Parse delay
    it = json.object_value().find("delay");
    if (it != json.object_value().end()) {
      if (!ParseDurationFromJson(it->second, &fault_injection_policy.delay)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:delay error:Failed parsing"));
      };
    }
    // Parse delay_header
    fault_injection_policy.delay_header =
        ParseStringField(json, "delayHeader", &error_list);
    // Parse delay_percentage_header
    fault_injection_policy.delay_percentage_header =
        ParseStringField(json, "delayPercentageHeader", &error_list);
    // Parse delay_percentage_denominator
    it = json.object_value().find("delayPercentageDenominator");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:delayPercentageDenominator error:should be of type number"));
      } else {
        fault_injection_policy.delay_percentage_denominator =
            gpr_parse_nonnegative_int(it->second.string_value().c_str());
        if (fault_injection_policy.delay_percentage_denominator != 100 &&
            fault_injection_policy.delay_percentage_denominator != 10000 &&
            fault_injection_policy.delay_percentage_denominator != 1000000) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:delayPercentageDenominator error:Denominator can only be "
              "one of "
              "100, 10000, 1000000"));
        }
      }
    }
    // Parse max_faults
    it = json.object_value().find("maxFaults");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxFaults error:should be of type number"));
      } else {
        fault_injection_policy.max_faults =
            gpr_parse_nonnegative_int(it->second.string_value().c_str());
        if (fault_injection_policy.max_faults < 0) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:maxFaults error:should be zero or positive"));
        }
      }
    }
    policies.push_back(std::move(fault_injection_policy));
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("faultInjectionPolicy", &error_list);
  return policies;
}

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
FaultInjectionServiceConfigParser::ParsePerMethodParams(
    const grpc_channel_args* args, const Json& json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  // Parse fault injection policy.
  if (!grpc_channel_args_find_bool(
          args, GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG, false)) {
    return nullptr;
  }
  auto it = json.object_value().find("faultInjectionPolicy");
  if (it == json.object_value().end()) {
    return nullptr;
  }
  std::vector<FaultInjectionPolicy> fault_injection_policies =
      ParseFaultInjectionPolicy(it->second, error);
  if (fault_injection_policies.empty() || *error != GRPC_ERROR_NONE) {
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
