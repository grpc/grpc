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

#include "src/core/ext/xds/xds_http_filters.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "envoy/extensions/filters/common/fault/v3/fault.upb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.upb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.upbdefs.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/ext/filters/client_channel/fault_injection_filter.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/transport/status_conversion.h"

namespace grpc_core {

const char* kXdsHttpRouterFilterConfigName =
    "envoy.extensions.filters.http.router.v3.Router";
const char* kXdsHttpFaultFilterConfigName =
    "envoy.extensions.filters.http.fault.v3.HTTPFault";

namespace {

absl::StatusOr<uint32_t> FractionPercentParseToPerMillion(
    const envoy_type_v3_FractionalPercent* fraction) {
  if (fraction != nullptr) {
    uint32_t numerator = envoy_type_v3_FractionalPercent_numerator(fraction);
    const auto denominator =
        static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
            envoy_type_v3_FractionalPercent_denominator(fraction));
    // Normalize to million.
    switch (denominator) {
      case envoy_type_v3_FractionalPercent_HUNDRED:
        numerator *= 10000;
        break;
      case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
        numerator *= 100;
        break;
      case envoy_type_v3_FractionalPercent_MILLION:
        break;
      default:
        return absl::InvalidArgumentError("unknown denominator type");
    }
    if (numerator > 1000000) {
      return absl::InvalidArgumentError(absl::StrCat(
          "invalid fraction percent: ", numerator, "/", denominator));
    }
    return numerator;
  }
  return 0;
}

uint32_t GetDenominator(const envoy_type_v3_FractionalPercent* fraction) {
  if (fraction != nullptr) {
    const auto denominator =
        static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
            envoy_type_v3_FractionalPercent_denominator(fraction));
    switch (denominator) {
      case envoy_type_v3_FractionalPercent_MILLION:
        return 1000000;
      case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
        return 10000;
      case envoy_type_v3_FractionalPercent_HUNDRED:
      default:
        return 100;
    }
  }
  // Use 100 as the default denominator
  return 100;
}

class XdsHttpRouterFilter : public XdsHttpFilterImpl {
 public:
  void PopulateSymtab(upb_symtab* symtab) const override {
    envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_filter_config, upb_arena* arena) const override {
    if (envoy_extensions_filters_http_router_v3_Router_parse(
            serialized_filter_config.data, serialized_filter_config.size,
            arena) == nullptr) {
      return absl::InvalidArgumentError("could not parse router filter config");
    }
    return FilterConfig{kXdsHttpRouterFilterConfigName, Json()};
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview /*serialized_filter_config*/,
      upb_arena* /*arena*/) const override {
    return absl::InvalidArgumentError(
        "router filter does not support config override");
  }

  // No-op -- this filter is special-cased by the xds resolver.
  const grpc_channel_filter* channel_filter() const override { return nullptr; }

  // No-op -- this filter is special-cased by the xds resolver.
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return absl::UnimplementedError("router filter should never be called");
  }
};

class XdsHttpFaultFilter : public XdsHttpFilterImpl {
 public:
  void PopulateSymtab(upb_symtab* symtab) const override {
    envoy_extensions_filters_http_fault_v3_HTTPFault_getmsgdef(symtab);
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_filter_config, upb_arena* arena) const override {
    absl::StatusOr<Json> parse_result =
        ParseHttpFaultIntoJson(serialized_filter_config, arena);
    if (!parse_result.ok()) {
      return parse_result.status();
    }
    return FilterConfig{kXdsHttpFaultFilterConfigName, *parse_result};
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview serialized_filter_config, upb_arena* arena) const override {
    // HTTPFault filter has the same message type in HTTP connection manager's
    // filter config and in overriding filter config field.
    return GenerateFilterConfig(serialized_filter_config, arena);
  }

  const grpc_channel_filter* channel_filter() const override {
    return &grpc_fault_injection_filter;
  }

  grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) const override {
    grpc_arg args_to_add = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG), 1);
    grpc_channel_args* new_args =
        grpc_channel_args_copy_and_add(args, &args_to_add, 1);
    grpc_channel_args_destroy(args);
    return new_args;
  }

  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& hcm_filter_config,
      const FilterConfig* filter_config_override) const override {
    Json policy_json = filter_config_override != nullptr
                           ? filter_config_override->config
                           : hcm_filter_config.config;
    // The policy JSON may be empty, that's allowed.
    return ServiceConfigJsonEntry{"faultInjectionPolicy", policy_json.Dump()};
  }

 private:
  absl::StatusOr<Json> ParseHttpFaultIntoJson(upb_strview serialized_http_fault,
                                              upb_arena* arena) const {
    auto* http_fault = envoy_extensions_filters_http_fault_v3_HTTPFault_parse(
        serialized_http_fault.data, serialized_http_fault.size, arena);
    if (http_fault == nullptr) {
      return absl::InvalidArgumentError(
          "could not parse fault injection filter config");
    }
    // NOTE(lidiz): Here, we are manually translating the upb messages into the
    // JSON form of the filter config as part of method config, which will be
    // directly used later by service config. In this way, we can validate the
    // filter configs, and NACK if needed. It also allows the service config to
    // function independently without xDS, but not the other way around.
    // NOTE(lidiz): please refer to
    // ClientChannelMethodParsedConfig::FaultInjectionPolicy for ground truth
    // definitions, located at:
    // src/core/ext/filters/client_channel/resolver_result_parsing.cc
    Json::Object fault_injection_policy_json;
    // Section 1: Parse the abort injection config
    const auto* fault_abort =
        envoy_extensions_filters_http_fault_v3_HTTPFault_abort(http_fault);
    if (fault_abort != nullptr) {
      grpc_status_code abort_grpc_status_code = GRPC_STATUS_OK;
      // Try if gRPC status code is set first
      int abort_grpc_status_code_raw =
          envoy_extensions_filters_http_fault_v3_FaultAbort_grpc_status(
              fault_abort);
      if (abort_grpc_status_code_raw != 0) {
        if (!grpc_status_code_from_int(abort_grpc_status_code_raw,
                                       &abort_grpc_status_code)) {
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid gRPC status code: ", abort_grpc_status_code_raw));
        }
      } else {
        // if gRPC status code is empty, check http status
        int abort_http_status_code =
            envoy_extensions_filters_http_fault_v3_FaultAbort_http_status(
                fault_abort);
        if (abort_http_status_code != 0 and abort_http_status_code != 200) {
          abort_grpc_status_code =
              grpc_http2_status_to_grpc_status(abort_http_status_code);
        }
      }
      // Set the abort_code, even if it's OK
      fault_injection_policy_json["abortCode"] =
          grpc_status_code_to_string(abort_grpc_status_code);
      // Set the headers if we enabled header abort injection control
      if (envoy_extensions_filters_http_fault_v3_FaultAbort_has_header_abort(
              fault_abort)) {
        fault_injection_policy_json["abortCodeHeader"] =
            "x-envoy-fault-abort-grpc-request";
        fault_injection_policy_json["abortPercentageHeader"] =
            "x-envoy-fault-abort-percentage";
      }
      // Set the fraction percent
      auto* percent =
          envoy_extensions_filters_http_fault_v3_FaultAbort_percentage(
              fault_abort);
      absl::StatusOr<uint32_t> fraction_parse_result =
          FractionPercentParseToPerMillion(percent);
      if (!fraction_parse_result.ok()) {
        return fraction_parse_result.status();
      }
      fault_injection_policy_json["abortPerMillion"] =
          Json(*fraction_parse_result);
      fault_injection_policy_json["abortPercentageDenominator"] =
          Json(GetDenominator(percent));
    }
    // Section 2: Parse the delay injection config
    const auto* fault_delay =
        envoy_extensions_filters_http_fault_v3_HTTPFault_delay(http_fault);
    if (fault_delay != nullptr) {
      // Parse the delay duration
      const auto* delay_duration =
          envoy_extensions_filters_common_fault_v3_FaultDelay_fixed_delay(
              fault_delay);
      if (delay_duration != nullptr) {
        fault_injection_policy_json["delay"] = absl::StrFormat(
            "%d.%09ds", google_protobuf_Duration_seconds(delay_duration),
            google_protobuf_Duration_nanos(delay_duration));
      }
      // Set the headers if we enabled header delay injection control
      if (envoy_extensions_filters_common_fault_v3_FaultDelay_has_header_delay(
              fault_delay)) {
        fault_injection_policy_json["delayHeader"] =
            "x-envoy-fault-delay-request";
        fault_injection_policy_json["delayPercentageHeader"] =
            "x-envoy-fault-delay-request-percentage";
      }
      // Set the fraction percent
      auto* percent =
          envoy_extensions_filters_common_fault_v3_FaultDelay_percentage(
              fault_delay);
      absl::StatusOr<uint32_t> fraction_parse_result =
          FractionPercentParseToPerMillion(percent);
      if (!fraction_parse_result.ok()) {
        return fraction_parse_result.status();
      }
      fault_injection_policy_json["delayPerMillion"] =
          Json(*fraction_parse_result);
      fault_injection_policy_json["delayPercentageDenominator"] =
          Json(GetDenominator(percent));
    }
    // Section 3: Parse the maximum active faults
    const auto* max_fault_wrapper =
        envoy_extensions_filters_http_fault_v3_HTTPFault_max_active_faults(
            http_fault);
    if (max_fault_wrapper != nullptr) {
      fault_injection_policy_json["maxFaults"] =
          google_protobuf_UInt32Value_value(max_fault_wrapper);
    }
    return fault_injection_policy_json;
  }
};

using FilterOwnerList = std::vector<std::unique_ptr<XdsHttpFilterImpl>>;
using FilterRegistryMap = std::map<absl::string_view, XdsHttpFilterImpl*>;

FilterOwnerList* g_filters = nullptr;
FilterRegistryMap* g_filter_registry = nullptr;

}  // namespace

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter,
    const std::set<absl::string_view>& config_proto_type_names) {
  for (auto config_proto_type_name : config_proto_type_names) {
    (*g_filter_registry)[config_proto_type_name] = filter.get();
  }
  g_filters->push_back(std::move(filter));
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) {
  auto it = g_filter_registry->find(proto_type_name);
  if (it == g_filter_registry->end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_symtab* symtab) {
  for (const auto& filter : *g_filters) {
    filter->PopulateSymtab(symtab);
  }
}

void XdsHttpFilterRegistry::Init() {
  g_filters = new FilterOwnerList;
  g_filter_registry = new FilterRegistryMap;
  RegisterFilter(absl::make_unique<XdsHttpRouterFilter>(),
                 {kXdsHttpRouterFilterConfigName});
  RegisterFilter(absl::make_unique<XdsHttpFaultFilter>(),
                 {kXdsHttpFaultFilterConfigName});
}

void XdsHttpFilterRegistry::Shutdown() {
  delete g_filter_registry;
  delete g_filters;
}

}  // namespace grpc_core
