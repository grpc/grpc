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

#include "src/core/ext/xds/xds_bootstrap_grpc.h"

#include <stdlib.h>

#include <algorithm>
#include <set>
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
#include "absl/types/optional.h"

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/security/certificate_provider/certificate_provider_factory.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"

namespace grpc_core {

//
// GrpcXdsBootstrap::GrpcNode::Locality
//

const JsonLoaderInterface* GrpcXdsBootstrap::GrpcNode::Locality::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<Locality>()
          .OptionalField("region", &Locality::region)
          .OptionalField("zone", &Locality::zone)
          .OptionalField("sub_zone", &Locality::sub_zone)
          .Finish();
  return loader;
}

//
// GrpcXdsBootstrap::GrpcNode
//

const JsonLoaderInterface* GrpcXdsBootstrap::GrpcNode::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcNode>()
          .OptionalField("id", &GrpcNode::id_)
          .OptionalField("cluster", &GrpcNode::cluster_)
          .OptionalField("locality", &GrpcNode::locality_)
          .OptionalField("metadata", &GrpcNode::metadata_)
          .Finish();
  return loader;
}

//
// GrpcXdsBootstrap::GrpcXdsServer::ChannelCreds
//

const JsonLoaderInterface*
GrpcXdsBootstrap::GrpcXdsServer::ChannelCreds::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ChannelCreds>()
          .Field("type", &ChannelCreds::type)
          .OptionalField("config", &ChannelCreds::config)
          .Finish();
  return loader;
}

//
// GrpcXdsBootstrap::GrpcXdsServer
//

namespace {

constexpr absl::string_view kServerFeatureXdsV3 = "xds_v3";
constexpr absl::string_view kServerFeatureIgnoreResourceDeletion =
    "ignore_resource_deletion";

}  // namespace

bool GrpcXdsBootstrap::GrpcXdsServer::ShouldUseV3() const {
  return server_features_.find(std::string(kServerFeatureXdsV3)) !=
         server_features_.end();
}

bool GrpcXdsBootstrap::GrpcXdsServer::IgnoreResourceDeletion() const {
  return server_features_.find(std::string(
             kServerFeatureIgnoreResourceDeletion)) != server_features_.end();
}

bool GrpcXdsBootstrap::GrpcXdsServer::Equals(const XdsServer& other) const {
  const auto& o = static_cast<const GrpcXdsServer&>(other);
  return (server_uri_ == o.server_uri_ &&
          channel_creds_.type == o.channel_creds_.type &&
          channel_creds_.config == o.channel_creds_.config &&
          server_features_ == o.server_features_);
}

const JsonLoaderInterface* GrpcXdsBootstrap::GrpcXdsServer::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcXdsServer>()
          .Field("server_uri", &GrpcXdsServer::server_uri_)
          .Finish();
  return loader;
}

void GrpcXdsBootstrap::GrpcXdsServer::JsonPostLoad(const Json& json,
                                                   const JsonArgs& args,
                                                   ValidationErrors* errors) {
  // Parse "channel_creds".
  auto channel_creds_list = LoadJsonObjectField<std::vector<ChannelCreds>>(
      json.object_value(), args, "channel_creds", errors);
  if (channel_creds_list.has_value()) {
    ValidationErrors::ScopedField field(errors, ".channel_creds");
    for (size_t i = 0; i < channel_creds_list->size(); ++i) {
      ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
      auto& creds = (*channel_creds_list)[i];
      // Select the first channel creds type that we support.
      if (channel_creds_.type.empty() &&
          CoreConfiguration::Get().channel_creds_registry().IsSupported(
              creds.type)) {
        if (!CoreConfiguration::Get().channel_creds_registry().IsValidConfig(
                creds.type, creds.config)) {
          errors->AddError(absl::StrCat(
              "invalid config for channel creds type \"", creds.type, "\""));
          continue;
        }
        channel_creds_.type = std::move(creds.type);
        channel_creds_.config = std::move(creds.config);
      }
    }
    if (channel_creds_.type.empty()) {
      errors->AddError("no known creds type found");
    }
  }
  // Parse "server_features".
  {
    ValidationErrors::ScopedField field(errors, ".server_features");
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
            server_features_.insert(feature_json.string_value());
          }
        }
      }
    }
  }
}

Json GrpcXdsBootstrap::GrpcXdsServer::ToJson() const {
  Json::Object channel_creds_json{{"type", channel_creds_.type}};
  if (!channel_creds_.config.empty()) {
    channel_creds_json["config"] = channel_creds_.config;
  }
  Json::Object json{
      {"server_uri", server_uri_},
      {"channel_creds", Json::Array{std::move(channel_creds_json)}},
  };
  if (!server_features_.empty()) {
    Json::Array server_features_json;
    for (auto& feature : server_features_) {
      server_features_json.emplace_back(feature);
    }
    json["server_features"] = std::move(server_features_json);
  }
  return json;
}

//
// GrpcXdsBootstrap::GrpcAuthority
//

const JsonLoaderInterface* GrpcXdsBootstrap::GrpcAuthority::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcAuthority>()
          .OptionalField(
              "client_listener_resource_name_template",
              &GrpcAuthority::client_listener_resource_name_template_)
          .OptionalField("xds_servers", &GrpcAuthority::servers_)
          .Finish();
  return loader;
}

//
// GrpcXdsBootstrap
//

absl::StatusOr<std::unique_ptr<GrpcXdsBootstrap>> GrpcXdsBootstrap::Create(
    absl::string_view json_string) {
  auto json = Json::Parse(json_string);
  if (!json.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse bootstrap JSON string: ", json.status().ToString()));
  }
  // Validate JSON.
  class XdsJsonArgs : public JsonArgs {
   public:
    bool IsEnabled(absl::string_view key) const override {
      if (key == "federation") return XdsFederationEnabled();
      return true;
    }
  };
  auto bootstrap = LoadFromJson<GrpcXdsBootstrap>(*json, XdsJsonArgs());
  if (!bootstrap.ok()) return bootstrap.status();
  return absl::make_unique<GrpcXdsBootstrap>(std::move(*bootstrap));
}

const JsonLoaderInterface* GrpcXdsBootstrap::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcXdsBootstrap>()
          .Field("xds_servers", &GrpcXdsBootstrap::servers_)
          .OptionalField("node", &GrpcXdsBootstrap::node_)
          .OptionalField("certificate_providers",
                         &GrpcXdsBootstrap::certificate_providers_)
          .OptionalField(
              "server_listener_resource_name_template",
              &GrpcXdsBootstrap::server_listener_resource_name_template_)
          .OptionalField("authorities", &GrpcXdsBootstrap::authorities_,
                         "federation")
          .OptionalField("client_default_listener_resource_name_template",
                         &GrpcXdsBootstrap::
                             client_default_listener_resource_name_template_,
                         "federation")
          .Finish();
  return loader;
}

void GrpcXdsBootstrap::JsonPostLoad(const Json& /*json*/,
                                    const JsonArgs& /*args*/,
                                    ValidationErrors* errors) {
  // Verify that each authority has the right prefix in the
  // client_listener_resource_name_template field.
  {
    ValidationErrors::ScopedField field(errors, ".authorities");
    for (const auto& p : authorities_) {
      const std::string& name = p.first;
      const GrpcAuthority& authority =
          static_cast<const GrpcAuthority&>(p.second);
      ValidationErrors::ScopedField field(
          errors, absl::StrCat("[\"", name,
                               "\"].client_listener_resource_name_template"));
      std::string expected_prefix = absl::StrCat("xdstp://", name, "/");
      if (!authority.client_listener_resource_name_template().empty() &&
          !absl::StartsWith(authority.client_listener_resource_name_template(),
                            expected_prefix)) {
        errors->AddError(
            absl::StrCat("field must begin with \"", expected_prefix, "\""));
      }
    }
  }
}

std::string GrpcXdsBootstrap::ToString() const {
  std::vector<std::string> parts;
  if (node_.has_value()) {
    parts.push_back(
        absl::StrFormat("node={\n"
                        "  id=\"%s\",\n"
                        "  cluster=\"%s\",\n"
                        "  locality={\n"
                        "    region=\"%s\",\n"
                        "    zone=\"%s\",\n"
                        "    sub_zone=\"%s\"\n"
                        "  },\n"
                        "  metadata=%s,\n"
                        "},\n",
                        node_->id(), node_->cluster(), node_->locality_region(),
                        node_->locality_zone(), node_->locality_sub_zone(),
                        Json{node_->metadata()}.Dump()));
  }
  parts.push_back(
      absl::StrFormat("servers=[\n%s\n],\n", servers_[0].ToJson().Dump()));
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
                        entry.second.client_listener_resource_name_template()));
    if (entry.second.server() != nullptr) {
      parts.push_back(absl::StrFormat(
          "    servers=[\n%s\n],\n",
          static_cast<const GrpcXdsServer*>(entry.second.server())
              ->ToJson()
              .Dump()));
    }
    parts.push_back("      },\n");
  }
  parts.push_back("}\n");
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

const XdsBootstrap::Authority* GrpcXdsBootstrap::LookupAuthority(
    const std::string& name) const {
  auto it = authorities_.find(name);
  if (it != authorities_.end()) {
    return &it->second;
  }
  return nullptr;
}

const XdsBootstrap::XdsServer* GrpcXdsBootstrap::FindXdsServer(
    const XdsBootstrap::XdsServer& server) const {
  if (static_cast<const GrpcXdsServer&>(server) == servers_[0]) {
    return &servers_[0];
  }
  for (auto& p : authorities_) {
    const auto* authority_server =
        static_cast<const GrpcXdsServer*>(p.second.server());
    if (authority_server != nullptr && *authority_server == server) {
      return authority_server;
    }
  }
  return nullptr;
}

}  // namespace grpc_core
