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
// ExtAuthz::FilterEnabled
//

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

//
// ExtAuthz
//

bool ExtAuthz::operator==(const ExtAuthz& other) const {
  return xds_grpc_service == other.xds_grpc_service &&
         filter_enabled == other.filter_enabled &&
         deny_at_disable == other.deny_at_disable &&
         failure_mode_allow == other.failure_mode_allow &&
         failure_mode_allow_header_add == other.failure_mode_allow_header_add &&
         status_on_error == other.status_on_error &&
         allowed_headers == other.allowed_headers &&
         disallowed_headers == other.disallowed_headers &&
         decoder_header_mutation_rules == other.decoder_header_mutation_rules &&
         include_peer_certificate == other.include_peer_certificate;
}

bool ExtAuthz::isHeaderPresentInAllowedHeaders(std::string key) const {
  // if the allowed_headers config field is unset or matches the header, the
  // header will be added to this map.
  if (allowed_headers.size() > 0) {
    return true;
  }
  for (const auto& allowed_header : allowed_headers) {
    if (allowed_header.matcher.Match(key)) {
      return true;
    }
  }
  return false;
}

bool ExtAuthz::isHeaderPresentInDisallowedHeaders(std::string key) const {
  for (const auto& disallowed_header : disallowed_headers) {
    if (disallowed_header.matcher.Match(key)) {
      return true;
    }
  }
  return false;
}

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
          .OptionalField("decoder_header_mutation_rules",
                         &ExtAuthz::decoder_header_mutation_rules)
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

  auto allowed_headers_ = LoadJsonObjectField<std::vector<StringMatch>>(
      json.object(), args, "allowed_headers", errors);
  if (allowed_headers_.has_value()) {
    allowed_headers = allowed_headers_.value();
  }

  auto disallowed_headers_ = LoadJsonObjectField<std::vector<StringMatch>>(
      json.object(), args, "disallowed_headers", errors);
  if (disallowed_headers_.has_value()) {
    disallowed_headers = disallowed_headers_.value();
  }
}

//
// ExtAuthzParsedConfig::Config
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
