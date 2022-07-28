//
// Copyright 2019 gRPC authors.
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

#include "src/core/ext/xds/xds_bootstrap.h"

#include <stdlib.h>

#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>

#include "src/core/ext/xds/certificate_provider_factory.h"
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json_util.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"

namespace grpc_core {

// TODO(donnadionne): check to see if federation is enabled, this will be
// removed once federation is fully integrated and enabled by default.
bool XdsFederationEnabled() {
  char* value = gpr_getenv("GRPC_EXPERIMENTAL_XDS_FEDERATION");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

namespace {

const absl::string_view kServerFeatureXdsV3 = "xds_v3";
const absl::string_view kServerFeatureIgnoreResourceDeletion =
    "ignore_resource_deletion";

}  // namespace

//
// XdsBootstrap::Node::Locality
//

const JsonLoaderInterface* XdsBootstrap::Node::Locality::JsonLoader() {
  static const auto loader = JsonObjectLoader<Locality>()
                                 .OptionalField("region", &Locality::region)
                                 .OptionalField("zone", &Locality::zone)
                                 .OptionalField("sub_zone", &Locality::sub_zone)
                                 .Finish();
  return &loader;
}

//
// XdsBootstrap::Node
//

const JsonLoaderInterface* XdsBootstrap::Node::JsonLoader() {
  static const auto loader = JsonObjectLoader<Node>()
                                 .OptionalField("id", &Node::id)
                                 .OptionalField("cluster", &Node::cluster)
                                 .OptionalField("locality", &Node::locality)
                                 .OptionalField("metadata", &Node::metadata)
                                 .Finish();
  return &loader;
}

//
// XdsBootstrap::XdsServer
//

namespace {

// FIXME: move this into the public API?
struct XdsChannelCreds {
  std::string type;
  Json config;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto loader =
        JsonObjectLoader<XdsChannelCreds>()
            .Field("type", &XdsChannelCreds::type)
            .OptionalField("config", &XdsChannelCreds::config)
            .Finish();
    return &loader;
  }
};

}  // namespace

const JsonLoaderInterface* XdsBootstrap::XdsServer::JsonLoader() {
  static const auto loader = JsonObjectLoader<XdsServer>()
                                 .Field("server_uri", &XdsServer::server_uri)
                                 .Finish();
  return &loader;
}

void XdsBootstrap::XdsServer::JsonPostLoad(const Json& json,
                                           ErrorList* errors) {
  // Parse "channel_creds".
  {
    ScopedField field(errors, ".channel_creds");
    auto it = json.object_value().find("channel_creds");
    if (it == json.object_value().end()) {
      errors->AddError("field not present");
    } else if (it->second.type() != Json::Type::ARRAY) {
      errors->AddError("is not an array");
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        ScopedField field(errors, absl::StrCat("[", i, "]"));
        auto channel_creds = LoadFromJson<XdsChannelCreds>(array[i]);
        if (!channel_creds.ok()) {
          errors->AddError(channel_creds.status().message());
          continue;
        }
        // Select the first channel creds type that we support.
        if (channel_creds_type.empty() &&
            CoreConfiguration::Get().channel_creds_registry().IsSupported(
                channel_creds->type)) {
          if (!CoreConfiguration::Get().channel_creds_registry().IsValidConfig(
                  channel_creds->type, channel_creds->config)) {
            errors->AddError(
                absl::StrCat("invalid config for channel creds type \"",
                             channel_creds->type, "\""));
            continue;
          }
          channel_creds_type = std::move(channel_creds->type);
          channel_creds_config = std::move(channel_creds->config);
        }
      }
      if (channel_creds_type.empty()) {
        errors->AddError("no known creds type found");
      }
    }
  }
  // Parse "server_features".
  {
    ScopedField field(errors, ".server_features");
    auto it = json.object_value().find("server_features");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::ARRAY) {
        errors->AddError("is not an array");
      } else {
        const Json::Array& array = it->second.array_value();
        for (const Json& feature_json : array) {
          if (feature_json.type() == Json::Type::STRING &&
              (feature_json.string_value() == kServerFeatureXdsV3 ||
               feature_json.string_value() ==
                   kServerFeatureIgnoreResourceDeletion)) {
            server_features.insert(feature_json.string_value());
          }
        }
      }
    }
  }
}

Json::Object XdsBootstrap::XdsServer::ToJson() const {
  Json::Object channel_creds_json{{"type", channel_creds_type}};
  if (channel_creds_config.type() != Json::Type::JSON_NULL) {
    channel_creds_json["config"] = channel_creds_config;
  }
  Json::Object json{
      {"server_uri", server_uri},
      {"channel_creds", Json::Array{std::move(channel_creds_json)}},
  };
  if (!server_features.empty()) {
    Json::Array server_features_json;
    for (auto& feature : server_features) {
      server_features_json.emplace_back(feature);
    }
    json["server_features"] = std::move(server_features_json);
  }
  return json;
}

bool XdsBootstrap::XdsServer::ShouldUseV3() const {
  return server_features.find(std::string(kServerFeatureXdsV3)) !=
         server_features.end();
}

bool XdsBootstrap::XdsServer::IgnoreResourceDeletion() const {
  return server_features.find(std::string(
             kServerFeatureIgnoreResourceDeletion)) != server_features.end();
}

//
// XdsBootstrap::Authority
//

const JsonLoaderInterface* XdsBootstrap::Authority::JsonLoader() {
  static const auto loader =
      JsonObjectLoader<Authority>()
          .OptionalField("client_listener_resource_name_template",
                         &Authority::client_listener_resource_name_template)
          .OptionalField("xds_servers", &Authority::xds_servers)
          .Finish();
  return &loader;
}

//
// XdsBootstrap
//

absl::StatusOr<std::unique_ptr<XdsBootstrap>> XdsBootstrap::Create(
    absl::string_view json_string) {
  auto json = Json::Parse(json_string);
  if (!json.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse bootstrap JSON string: ", json.status().message()));
  }
  auto bootstrap = absl::make_unique<XdsBootstrap>();
  absl::Status status = LoadFromJson<XdsBootstrap>(*json, bootstrap.get());
  if (!status.ok()) return status;
  return bootstrap;
}

const JsonLoaderInterface* XdsBootstrap::JsonLoader() {
  if (XdsFederationEnabled()) {
    static const auto loader =
        JsonObjectLoader<XdsBootstrap>()
            .Field("xds_servers", &XdsBootstrap::servers_)
            .OptionalField("node", &XdsBootstrap::node_)
            .OptionalField("certificate_providers",
                           &XdsBootstrap::certificate_providers_)
            .OptionalField(
                "server_listener_resource_name_template",
                &XdsBootstrap::server_listener_resource_name_template_)
            .OptionalField("authorities", &XdsBootstrap::authorities_)
            .OptionalField(
                "client_default_listener_resource_name_template",
                &XdsBootstrap::client_default_listener_resource_name_template_)
            .Finish();
    return &loader;
  }
  static const auto loader =
      JsonObjectLoader<XdsBootstrap>()
          .Field("xds_servers", &XdsBootstrap::servers_)
          .OptionalField("node", &XdsBootstrap::node_)
          .OptionalField("certificate_providers",
                         &XdsBootstrap::certificate_providers_)
          .OptionalField("server_listener_resource_name_template",
                         &XdsBootstrap::server_listener_resource_name_template_)
          .Finish();
  return &loader;
}

void XdsBootstrap::JsonPostLoad(const Json& json, ErrorList* errors) {
  // Verify that each authority has the right prefix in the
  // client_listener_resource_name_template field.
  {
    ScopedField field(errors, ".authorities");
    for (const auto& p : authorities_) {
      const std::string& name = p.first;
      const Authority& authority = p.second;
      ScopedField field(
          errors, absl::StrCat("[\"", name,
                               "\"].client_listener_resource_name_template"));
      std::string expected_prefix = absl::StrCat("xdstp://", name, "/");
      if (!authority.client_listener_resource_name_template.empty() &&
          !absl::StartsWith(authority.client_listener_resource_name_template,
                            expected_prefix)) {
        errors->AddError(
            absl::StrCat("field must begin with \"", expected_prefix, "\""));
      }
    }
  }
}

const XdsBootstrap::Authority* XdsBootstrap::LookupAuthority(
    const std::string& name) const {
  auto it = authorities_.find(name);
  if (it != authorities_.end()) {
    return &it->second;
  }
  return nullptr;
}

bool XdsBootstrap::XdsServerExists(
    const XdsBootstrap::XdsServer& server) const {
  if (server == servers_[0]) return true;
  for (auto& authority : authorities_) {
    for (auto& xds_server : authority.second.xds_servers) {
      if (server == xds_server) return true;
    }
  }
  return false;
}

std::string XdsBootstrap::ToString() const {
  std::vector<std::string> parts;
  if (node_.has_value()) {
    parts.push_back(absl::StrFormat(
        "node={\n"
        "  id=\"%s\",\n"
        "  cluster=\"%s\",\n"
        "  locality={\n"
        "    region=\"%s\",\n"
        "    zone=\"%s\",\n"
        "    sub_zone=\"%s\"\n"
        "  },\n"
        "  metadata=%s,\n"
        "},\n",
        node_->id, node_->cluster, node_->locality.region, node_->locality.zone,
        node_->locality.sub_zone, node_->metadata.Dump()));
  }
  parts.push_back(
      absl::StrFormat("servers=[\n"
                      "  {\n"
                      "    uri=\"%s\",\n"
                      "    creds_type=%s,\n",
                      server().server_uri, server().channel_creds_type));
  if (server().channel_creds_config.type() != Json::Type::JSON_NULL) {
    parts.push_back(absl::StrFormat("    creds_config=%s,",
                                    server().channel_creds_config.Dump()));
  }
  if (!server().server_features.empty()) {
    parts.push_back(absl::StrCat("    server_features=[",
                                 absl::StrJoin(server().server_features, ", "),
                                 "],\n"));
  }
  parts.push_back("  }\n],\n");
  if (!client_default_listener_resource_name_template_.empty()) {
    parts.push_back(absl::StrFormat(
        "client_default_listener_resource_name_template=\"%s\",\n",
        client_default_listener_resource_name_template_));
  }
  if (!server_listener_resource_name_template_.empty()) {
    parts.push_back(
        absl::StrFormat("server_listener_resource_name_template=\"%s\",\n",
                        server_listener_resource_name_template_));
  }
  parts.push_back("authorities={\n");
  for (const auto& entry : authorities_) {
    parts.push_back(absl::StrFormat("  %s={\n", entry.first));
    parts.push_back(
        absl::StrFormat("    client_listener_resource_name_template=\"%s\",\n",
                        entry.second.client_listener_resource_name_template));
    parts.push_back(
        absl::StrFormat("    servers=[\n"
                        "      {\n"
                        "        uri=\"%s\",\n"
                        "        creds_type=%s,\n",
                        entry.second.xds_servers[0].server_uri,
                        entry.second.xds_servers[0].channel_creds_type));
    parts.push_back("      },\n");
  }
  parts.push_back("}");
  parts.push_back("certificate_providers={\n");
  for (const auto& entry : certificate_providers_) {
    parts.push_back(
        absl::StrFormat("  %s={\n"
                        "    plugin_name=%s\n"
                        "    config=%s\n"
                        "  },\n",
                        entry.first, entry.second.plugin_name,
                        entry.second.config->ToString()));
  }
  parts.push_back("}");
  return absl::StrJoin(parts, "");
}

}  // namespace grpc_core
