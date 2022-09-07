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

#include "src/core/ext/xds/certificate_provider_factory.h"
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"

namespace grpc_core {

namespace {

struct BootstrapJson {
  struct XdsServer {
    struct ChannelCreds {
      std::string type;
      Json::Object config;

      static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
        static const auto* loader =
            JsonObjectLoader<ChannelCreds>()
                .Field("type", &ChannelCreds::type)
                .OptionalField("config", &ChannelCreds::config)
                .Finish();
        return loader;
      }
    };

    std::string server_uri;
    ChannelCreds channel_creds;
    std::set<std::string> server_features;

    XdsBootstrap::XdsServer ToXdsServer() const {
      XdsBootstrap::XdsServer server;
      server.server_uri = server_uri;
      server.channel_creds_type = channel_creds.type;
      server.channel_creds_config = channel_creds.config;
      server.server_features = server_features;
      return server;
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<XdsServer>()
              .Field("server_uri", &XdsServer::server_uri)
              .Finish();
      return loader;
    }

    void JsonPostLoad(const Json& json, const JsonArgs& args,
                      ErrorList* errors) {
      // Parse "channel_creds".
      auto channel_creds_list = LoadJsonObjectField<std::vector<ChannelCreds>>(
          json.object_value(), args, "channel_creds", errors);
      if (channel_creds_list.has_value()) {
        ScopedField field(errors, ".channel_creds");
        for (size_t i = 0; i < channel_creds_list->size(); ++i) {
          ScopedField field(errors, absl::StrCat("[", i, "]"));
          auto& creds = (*channel_creds_list)[i];
          // Select the first channel creds type that we support.
          if (channel_creds.type.empty() &&
              CoreConfiguration::Get().channel_creds_registry().IsSupported(
                  creds.type)) {
            if (!CoreConfiguration::Get()
                     .channel_creds_registry()
                     .IsValidConfig(creds.type, creds.config)) {
              errors->AddError(
                  absl::StrCat("invalid config for channel creds type \"",
                               creds.type, "\""));
              continue;
            }
            channel_creds.type = std::move(creds.type);
            channel_creds.config = std::move(creds.config);
          }
        }
        if (channel_creds.type.empty()) {
          errors->AddError("no known creds type found");
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
                  (feature_json.string_value() ==
                       XdsBootstrap::XdsServer::kServerFeatureXdsV3 ||
                   feature_json.string_value() ==
                       XdsBootstrap::XdsServer::
                           kServerFeatureIgnoreResourceDeletion)) {
                server_features.insert(feature_json.string_value());
              }
            }
          }
        }
      }
    }
  };

  struct Node {
    struct Locality {
      std::string region;
      std::string zone;
      std::string sub_zone;

      static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
        static const auto* loader =
            JsonObjectLoader<Locality>()
                .OptionalField("region", &Locality::region)
                .OptionalField("zone", &Locality::zone)
                .OptionalField("sub_zone", &Locality::sub_zone)
                .Finish();
        return loader;
      }
    };

    std::string id;
    std::string cluster;
    Locality locality;
    Json::Object metadata;

    XdsBootstrap::Node ToNode() const {
      XdsBootstrap::Node node;
      node.id = id;
      node.cluster = cluster;
      node.locality_region = locality.region;
      node.locality_zone = locality.zone;
      node.locality_sub_zone = locality.sub_zone;
      node.metadata = metadata;
      return node;
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<Node>()
              .OptionalField("id", &Node::id)
              .OptionalField("cluster", &Node::cluster)
              .OptionalField("locality", &Node::locality)
              .OptionalField("metadata", &Node::metadata)
              .Finish();
      return loader;
    }
  };

  struct Authority {
    std::string client_listener_resource_name_template;
    std::vector<XdsServer> xds_servers;

    XdsBootstrap::Authority ToAuthority() const {
      XdsBootstrap::Authority authority;
      authority.client_listener_resource_name_template =
          client_listener_resource_name_template;
      for (const auto& server : xds_servers) {
        authority.xds_servers.push_back(server.ToXdsServer());
      }
      return authority;
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<Authority>()
              .OptionalField("client_listener_resource_name_template",
                             &Authority::client_listener_resource_name_template)
              .OptionalField("xds_servers", &Authority::xds_servers)
              .Finish();
      return loader;
    }
  };

  std::vector<XdsServer> servers;
  absl::optional<Node> node;
  std::string client_default_listener_resource_name_template;
  std::string server_listener_resource_name_template;
  std::map<std::string, Authority> authorities;
  CertificateProviderStore::PluginDefinitionMap certificate_providers;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<BootstrapJson>()
            .Field("xds_servers", &BootstrapJson::servers)
            .OptionalField("node", &BootstrapJson::node)
            .OptionalField("certificate_providers",
                           &BootstrapJson::certificate_providers)
            .OptionalField(
                "server_listener_resource_name_template",
                &BootstrapJson::server_listener_resource_name_template)
            .OptionalField("authorities", &BootstrapJson::authorities,
                           "federation")
            .OptionalField(
                "client_default_listener_resource_name_template",
                &BootstrapJson::client_default_listener_resource_name_template,
                "federation")
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json& /*json*/, const JsonArgs& /*args*/,
                    ErrorList* errors) {
    // Verify that each authority has the right prefix in the
    // client_listener_resource_name_template field.
    {
      ScopedField field(errors, ".authorities");
      for (const auto& p : authorities) {
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
};

}  // namespace

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
  auto bootstrap_json = LoadFromJson<BootstrapJson>(*json, XdsJsonArgs());
  if (!bootstrap_json.ok()) return bootstrap_json.status();
  std::vector<XdsServer> servers;
  for (auto& server : bootstrap_json->servers) {
    servers.emplace_back(server.ToXdsServer());
  }
  absl::optional<Node> node;
  if (bootstrap_json->node.has_value()) node = bootstrap_json->node->ToNode();
  std::map<std::string, Authority> authorities;
  for (const auto& p : bootstrap_json->authorities) {
    authorities[p.first] = p.second.ToAuthority();
  }
  return absl::make_unique<GrpcXdsBootstrap>(
      std::move(servers), std::move(node),
      std::move(bootstrap_json->client_default_listener_resource_name_template),
      std::move(bootstrap_json->server_listener_resource_name_template),
      std::move(authorities), std::move(bootstrap_json->certificate_providers));
}

GrpcXdsBootstrap::GrpcXdsBootstrap(
    std::vector<XdsServer> servers, absl::optional<Node> node,
    std::string client_default_listener_resource_name_template,
    std::string server_listener_resource_name_template,
    std::map<std::string, Authority> authorities,
    CertificateProviderStore::PluginDefinitionMap certificate_providers)
    : servers_(std::move(servers)),
      node_(std::move(node)),
      client_default_listener_resource_name_template_(
          std::move(client_default_listener_resource_name_template)),
      server_listener_resource_name_template_(
          std::move(server_listener_resource_name_template)),
      authorities_(std::move(authorities)),
      certificate_providers_(std::move(certificate_providers)) {}

std::string GrpcXdsBootstrap::ToString() const {
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
        node_->id, node_->cluster, node_->locality_region, node_->locality_zone,
        node_->locality_sub_zone, node_->metadata.Dump()));
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

absl::StatusOr<XdsBootstrap::XdsServer> GrpcXdsBootstrap::XdsServerParse(
    const Json& json) {
  auto xds_server = LoadFromJson<BootstrapJson::XdsServer>(json);
  if (!xds_server.ok()) return xds_server.status();
  return xds_server->ToXdsServer();
}

Json::Object GrpcXdsBootstrap::XdsServerToJson(const XdsServer& server) {
  Json::Object channel_creds_json{{"type", server.channel_creds_type}};
  if (server.channel_creds_config.type() != Json::Type::JSON_NULL) {
    channel_creds_json["config"] = server.channel_creds_config;
  }
  Json::Object json{
      {"server_uri", server.server_uri},
      {"channel_creds", Json::Array{std::move(channel_creds_json)}},
  };
  if (!server.server_features.empty()) {
    Json::Array server_features_json;
    for (auto& feature : server.server_features) {
      server_features_json.emplace_back(feature);
    }
    json["server_features"] = std::move(server_features_json);
  }
  return json;
}

}  // namespace grpc_core
