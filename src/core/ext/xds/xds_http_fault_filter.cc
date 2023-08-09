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

#include "src/core/ext/xds/xds_http_fault_filter.h"

#include <stdint.h>

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "envoy/extensions/filters/common/fault/v3/fault.upb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.upb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.upbdefs.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/wrappers.upb.h"

#include <grpc/status.h>
#include <grpc/support/json.h>

#include "src/core/ext/filters/fault_injection/fault_injection_filter.h"
#include "src/core/ext/filters/fault_injection/fault_injection_service_config_parser.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/transport/status_conversion.h"

namespace grpc_core {

namespace {

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

}  // namespace

absl::string_view XdsHttpFaultFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.fault.v3.HTTPFault";
}

absl::string_view XdsHttpFaultFilter::OverrideConfigProtoName() const {
  return "";
}

void XdsHttpFaultFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_fault_v3_HTTPFault_getmsgdef(symtab);
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpFaultFilter::GenerateFilterConfig(
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse fault injection filter config");
    return absl::nullopt;
  }
  auto* http_fault = envoy_extensions_filters_http_fault_v3_HTTPFault_parse(
      serialized_filter_config->data(), serialized_filter_config->size(),
      context.arena);
  if (http_fault == nullptr) {
    errors->AddError("could not parse fault injection filter config");
    return absl::nullopt;
  }
  // NOTE(lidiz): Here, we are manually translating the upb messages into the
  // JSON form of the filter config as part of method config, which will be
  // directly used later by service config. In this way, we can validate the
  // filter configs, and NACK if needed. It also allows the service config to
  // function independently without xDS, but not the other way around.
  // NOTE(lidiz): please refer to FaultInjectionPolicy for ground truth
  // definitions, located at:
  // src/core/ext/filters/fault_injection/service_config_parser.h
  Json::Object fault_injection_policy_json;
  // Section 1: Parse the abort injection config
  const auto* fault_abort =
      envoy_extensions_filters_http_fault_v3_HTTPFault_abort(http_fault);
  if (fault_abort != nullptr) {
    ValidationErrors::ScopedField field(errors, ".abort");
    grpc_status_code abort_grpc_status_code = GRPC_STATUS_OK;
    // Try if gRPC status code is set first
    int abort_grpc_status_code_raw =
        envoy_extensions_filters_http_fault_v3_FaultAbort_grpc_status(
            fault_abort);
    if (abort_grpc_status_code_raw != 0) {
      if (!grpc_status_code_from_int(abort_grpc_status_code_raw,
                                     &abort_grpc_status_code)) {
        ValidationErrors::ScopedField field(errors, ".grpc_status");
        errors->AddError(absl::StrCat("invalid gRPC status code: ",
                                      abort_grpc_status_code_raw));
      }
    } else {
      // if gRPC status code is empty, check http status
      int abort_http_status_code =
          envoy_extensions_filters_http_fault_v3_FaultAbort_http_status(
              fault_abort);
      if (abort_http_status_code != 0 && abort_http_status_code != 200) {
        abort_grpc_status_code =
            grpc_http2_status_to_grpc_status(abort_http_status_code);
      }
    }
    // Set the abort_code, even if it's OK
    fault_injection_policy_json["abortCode"] =
        Json::FromString(grpc_status_code_to_string(abort_grpc_status_code));
    // Set the headers if we enabled header abort injection control
    if (envoy_extensions_filters_http_fault_v3_FaultAbort_has_header_abort(
            fault_abort)) {
      fault_injection_policy_json["abortCodeHeader"] =
          Json::FromString("x-envoy-fault-abort-grpc-request");
      fault_injection_policy_json["abortPercentageHeader"] =
          Json::FromString("x-envoy-fault-abort-percentage");
    }
    // Set the fraction percent
    auto* percent =
        envoy_extensions_filters_http_fault_v3_FaultAbort_percentage(
            fault_abort);
    if (percent != nullptr) {
      fault_injection_policy_json["abortPercentageNumerator"] =
          Json::FromNumber(envoy_type_v3_FractionalPercent_numerator(percent));
      fault_injection_policy_json["abortPercentageDenominator"] =
          Json::FromNumber(GetDenominator(percent));
    }
  }
  // Section 2: Parse the delay injection config
  const auto* fault_delay =
      envoy_extensions_filters_http_fault_v3_HTTPFault_delay(http_fault);
  if (fault_delay != nullptr) {
    ValidationErrors::ScopedField field(errors, ".delay");
    // Parse the delay duration
    const auto* delay_duration =
        envoy_extensions_filters_common_fault_v3_FaultDelay_fixed_delay(
            fault_delay);
    if (delay_duration != nullptr) {
      ValidationErrors::ScopedField field(errors, ".fixed_delay");
      Duration duration = ParseDuration(delay_duration, errors);
      fault_injection_policy_json["delay"] =
          Json::FromString(duration.ToJsonString());
    }
    // Set the headers if we enabled header delay injection control
    if (envoy_extensions_filters_common_fault_v3_FaultDelay_has_header_delay(
            fault_delay)) {
      fault_injection_policy_json["delayHeader"] =
          Json::FromString("x-envoy-fault-delay-request");
      fault_injection_policy_json["delayPercentageHeader"] =
          Json::FromString("x-envoy-fault-delay-request-percentage");
    }
    // Set the fraction percent
    auto* percent =
        envoy_extensions_filters_common_fault_v3_FaultDelay_percentage(
            fault_delay);
    if (percent != nullptr) {
      fault_injection_policy_json["delayPercentageNumerator"] =
          Json::FromNumber(envoy_type_v3_FractionalPercent_numerator(percent));
      fault_injection_policy_json["delayPercentageDenominator"] =
          Json::FromNumber(GetDenominator(percent));
    }
  }
  // Section 3: Parse the maximum active faults
  const auto* max_fault_wrapper =
      envoy_extensions_filters_http_fault_v3_HTTPFault_max_active_faults(
          http_fault);
  if (max_fault_wrapper != nullptr) {
    fault_injection_policy_json["maxFaults"] =
        Json::FromNumber(google_protobuf_UInt32Value_value(max_fault_wrapper));
  }
  return FilterConfig{ConfigProtoName(),
                      Json::FromObject(std::move(fault_injection_policy_json))};
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpFaultFilter::GenerateFilterConfigOverride(
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  // HTTPFault filter has the same message type in HTTP connection manager's
  // filter config and in overriding filter config field.
  return GenerateFilterConfig(context, std::move(extension), errors);
}

const grpc_channel_filter* XdsHttpFaultFilter::channel_filter() const {
  return &FaultInjectionFilter::kFilter;
}

ChannelArgs XdsHttpFaultFilter::ModifyChannelArgs(
    const ChannelArgs& args) const {
  return args.Set(GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG, 1);
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsHttpFaultFilter::GenerateServiceConfig(
    const FilterConfig& hcm_filter_config,
    const FilterConfig* filter_config_override) const {
  Json policy_json = filter_config_override != nullptr
                         ? filter_config_override->config
                         : hcm_filter_config.config;
  // The policy JSON may be empty, that's allowed.
  return ServiceConfigJsonEntry{"faultInjectionPolicy", JsonDump(policy_json)};
}

}  // namespace grpc_core
