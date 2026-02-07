//
// Copyright 2026 gRPC authors.
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

#include "src/core/ext/filters/ext_authz/ext_authz_service_config_parser.h"

#include <grpc/support/port_platform.h>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/xds/grpc/xds_common_types.h"

namespace grpc_core {

//
// ExtAuthz
//

// Helper for StringMatcher
// void ParseStringMatcherList(const Json& json, const std::string& field_name,
//                             std::vector<StringMatcher>* output,
//                             ValidationErrors* errors) {
//   auto it = json.object().find(field_name);
//   if (it == json.object().end()) return;
//   if (it->second.type() != Json::Type::kArray) {
//     ValidationErrors::ScopedField field(errors, absl::StrCat(".",
//     field_name)); errors->AddError("must be an array"); return;
//   }
//   const auto& array = it->second.array();
//   for (size_t i = 0; i < array.size(); ++i) {
//     ValidationErrors::ScopedField field(
//         errors, absl::StrCat(".", field_name, "[", i, "]"));
//     const auto& item = array[i];
//     if (item.type() != Json::Type::kObject) {
//       errors->AddError("must be an object");
//       continue;
//     }
//     // Simple StringMatcher parsing for JSON
//     // { "exact": "..." } or { "prefix": "..." } or { "suffix": "..." } or {
//     // "contains": "..." } or { "safe_regex": "..." }
//     if (item.object().count("exact")) {
//       output->emplace_back(StringMatcher::Type::kExact,
//                            item.object().at("exact").string_value(),
//                            false);  // case_sensitive=true by default?
//     } else if (item.object().count("prefix")) {
//       output->emplace_back(StringMatcher::Type::kPrefix,
//                            item.object().at("prefix").string_value(), false);
//     } else if (item.object().count("suffix")) {
//       output->emplace_back(StringMatcher::Type::kSuffix,
//                            item.object().at("suffix").string_value(), false);
//     } else if (item.object().count("contains")) {
//       output->emplace_back(StringMatcher::Type::kContains,
//                            item.object().at("contains").string_value(),
//                            false);
//     } else if (item.object().count("safe_regex")) {
//       // TODO(rishesh): handle regex
//       // output->emplace_back(StringMatcher::Type::kSafeRegex,
//       // item.object().at("safe_regex").string_value(), false);
//     } else {
//       errors->AddError("unknown string matcher type");
//     }
//   }
// }

// const JsonLoaderInterface* ExtAuthz::JsonLoader(const JsonArgs&) {
//   static const auto* loader =
//       JsonObjectLoader<ExtAuthz>()
//           // manually handling grpc_service in JsonPostLoad
//           .OptionalField("failure_mode_allow", &ExtAuthz::failure_mode_allow)
//           .OptionalField("include_peer_certificate",
//                          &ExtAuthz::include_peer_certificate)
//           .OptionalField("status_on_error", &ExtAuthz::status_on_error)
//           .OptionalField("filter_enabled", &ExtAuthz::filter_enabled)
//           .OptionalField("deny_at_disable", &ExtAuthz::deny_at_disable)
//           .OptionalField("with_request_body", &ExtAuthz::with_request_body)
//           .OptionalField("metadata_context_namespaces",
//                          &ExtAuthz::metadata_context_namespaces)
//           .Finish();
//   return loader;
// }

// void ExtAuthz::JsonPostLoad(const Json& json, const JsonArgs& args,
//                             ValidationErrors* errors) {
//   // Parse grpc_service
//   {
//     ValidationErrors::ScopedField field(errors, ".grpc_service");
//     auto it = json.object().find("grpc_service");
//     if (it != json.object().end()) {
//       const auto& grpc_service_json = it->second;
//       if (grpc_service_json.type() != Json::Type::kObject) {
//          errors->AddError("must be an object");
//       } else {
//          // Check for google_grpc
//          auto it_google = grpc_service_json.object().find("google_grpc");
//          if (it_google != grpc_service_json.object().end()) {
//            ValidationErrors::ScopedField field(errors, ".google_grpc");
//            auto google_grpc_res =
//            LoadFromJson<JsonGoogleGrpc>(it_google->second, args, errors); //
//            Using LoadFromJson to trigger JsonLoader + JsonPostLoad
//            // Convert to XdsGrpcService
//            // We need to create GrpcXdsServerTarget
//            grpc_service.server_target =
//            std::make_unique<GrpcXdsServerTarget>(
//                google_grpc_res.target_uri,
//                std::move(google_grpc_res.channel_creds),
//                std::move(google_grpc_res.call_creds));
//          } else {
//              // Envoy also supports envoy_grpc, but we might only support
//              google_grpc for Service Config?
//              // Or maybe we should support envoy_grpc?
//              // For now, only google_grpc is standard in our tests.
//          }
//          // Timeout
//          auto it_timeout = grpc_service_json.object().find("timeout");
//          if (it_timeout != grpc_service_json.object().end()) {
//              // Parse Duration
//              // grpc_service.timeout = ParseDuration... requires upb/proto
//              usually.
//              // JSON duration is string "1.5s".
//              // We can use gpr_parse_duration?
//              // simple manual parsing for now if needed or leave 0.
//          }
//       }
//     } else {
//         // Required field?
//         // errors->AddError("field not present");
//         // But maybe optional?
//     }
//   }

//   // Parse allowed_headers/disallowed_headers
//   ParseStringMatcherList(json, "transport_api_version", nullptr,
//                          errors);  // ignored
//   // ParseStringMatcherList... implementation above needs to populate
//   // `allowed_headers`
//   ParseStringMatcherList(json, "allowed_headers", &allowed_headers, errors);
//   ParseStringMatcherList(json, "disallowed_headers", &disallowed_headers,
//                          errors);
// }

// bool ExtAuthz::HeaderMutationRules::operator==(
//     const HeaderMutationRules& other) const {
//   return disallow_all == other.disallow_all &&
//          disallow_is_error == other.disallow_is_error;
//   // Expressions comparison is hard (compiled RE2).
//   // Ignoring for equality check for now or assuming they come from same
//   source.
// }

bool ExtAuthz::operator==(const ExtAuthz& other) const {
  // return
  // grpc_service.server_target->Equals(*other.grpc_service.server_target) &&
  //        // timeout?
  //        filter_enabled == other.filter_enabled &&
  //        deny_at_disable == other.deny_at_disable &&
  //        failure_mode_allow == other.failure_mode_allow &&
  //        status_on_error == other.status_on_error &&
  //        include_peer_certificate == other.include_peer_certificate;
  //  TODO(rishesh): check others
  return true;
}

// const JsonLoaderInterface* ExtAuthz::HeaderMutationRules::JsonLoader(
//     const JsonArgs&) {
//     static const auto* loader = JsonObjectLoader<HeaderMutationRules>()
//         .OptionalField("disallow_all", &HeaderMutationRules::disallow_all)
//         .OptionalField("disallow_is_error",
//         &HeaderMutationRules::disallow_is_error) .Finish();
//     return loader;
// }

const JsonLoaderInterface* ExtAuthz::FilterEnabled::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<FilterEnabled>()
          .OptionalField("numerator", &FilterEnabled::numerator)
          .OptionalField("denominator", &FilterEnabled::denominator)
          .Finish();
  return loader;
}

void ExtAuthz::FilterEnabled::JsonPostLoad(const Json&, const JsonArgs&,
                                           ValidationErrors*) {}

const JsonLoaderInterface* ExtAuthz::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ExtAuthz>()
          .Field("xds_grpc_service", &ExtAuthz::xds_grpc_service)
          .OptionalField("filter_enabled", &ExtAuthz::filter_enabled)
          .OptionalField("deny_at_disable", &ExtAuthz::deny_at_disable)
          .OptionalField("failure_mode_allow", &ExtAuthz::failure_mode_allow)
          .OptionalField("failure_mode_allow_header_add",
                         &ExtAuthz::failure_mode_allow_header_add)
          .OptionalField("include_peer_certificate",
                         &ExtAuthz::include_peer_certificate)
          .Finish();
  return loader;
}

void ExtAuthz::JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors) {
  auto status =
      LoadJsonObjectField<int>(json.object(), args, "status_on_error", errors);
  if (status.has_value()) {
    status_on_error = grpc_http2_status_to_grpc_status(status.value());
  } else {
    ValidationErrors::ScopedField field(errors, ".ext_authz.status_on_error");
    errors->AddError("status_on_error field is not present");
  }
}

//
// ExtAuthzParsedConfig
//

const JsonLoaderInterface* ExtAuthzParsedConfig::Config::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<Config>()
          .Field("filter_instance_name", &Config::filter_instance_name)
          .Field("ext_authz", &Config::ext_authz)  // Nested loader needs
                                                   // ExtAuthz::JsonLoader
          .Finish();
  return loader;
}

void ExtAuthzParsedConfig::Config::JsonPostLoad(const Json&, const JsonArgs&,
                                                ValidationErrors*) {}

const JsonLoaderInterface* ExtAuthzParsedConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ExtAuthzParsedConfig>()
          .OptionalField("ext_authz", &ExtAuthzParsedConfig::configs_)
          .Finish();
  return loader;
}

// void ExtAuthzParsedConfig::ParseProtos(const ChannelArgs& args,
//                                        ValidationErrors* errors) {
//   // TODO: Implement ParseProtos
// }

//
// ExtAuthzServiceConfigParser
//

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ExtAuthzServiceConfigParser::ParseGlobalParams(const ChannelArgs& args,
                                               const Json& json,
                                               ValidationErrors* errors) {
  // Parse config from json.
  return LoadFromJson<std::unique_ptr<ExtAuthzParsedConfig>>(json, JsonArgs(),
                                                             errors);
}

std::unique_ptr<ServiceConfigParser::ParsedConfig>
ExtAuthzServiceConfigParser::ParsePerMethodParams(const ChannelArgs&,
                                                  const Json& json,
                                                  ValidationErrors* errors) {
  return LoadFromJson<std::unique_ptr<ExtAuthzParsedConfig>>(json, JsonArgs(),
                                                             errors);
}

void ExtAuthzServiceConfigParser::Register(
    CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<ExtAuthzServiceConfigParser>());
}

size_t ExtAuthzServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

}  // namespace grpc_core
