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

#include "src/core/xds/grpc/xds_bootstrap_grpc.h"

#include <grpc/support/json.h>
#include <stdlib.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"

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
  auto json = JsonParse(json_string);
  if (!json.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse bootstrap JSON string: ", json.status().ToString()));
  }
  // Validate JSON.
  class XdsJsonArgs final : public JsonArgs {
   public:
    bool IsEnabled(absl::string_view key) const override {
      if (key == "federation") return XdsFederationEnabled();
      return true;
    }
  };
  auto bootstrap = LoadFromJson<GrpcXdsBootstrap>(*json, XdsJsonArgs());
  if (!bootstrap.ok()) return bootstrap.status();
  return std::make_unique<GrpcXdsBootstrap>(std::move(*bootstrap));
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
  // Verify that there is at least one server present.
  {
    ValidationErrors::ScopedField field(errors, ".xds_servers");
    if (servers_.empty() && !errors->FieldHasErrors()) {
      errors->AddError("must be non-empty");
    }
  }
  // Verify that each authority has the right prefix in the
  // client_listener_resource_name_template field.
  {
    ValidationErrors::ScopedField field(errors, ".authorities");
    for (const auto& [name, authority] : authorities_) {
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
                        JsonDump(Json::FromObject(node_->metadata()))));
  }
  std::vector<std::string> server_strings;
  for (auto& server : servers_) {
    server_strings.emplace_back(server.Key());
  }
  parts.push_back(absl::StrFormat("    servers=[\n%s\n],\n",
                                  absl::StrJoin(server_strings, ",\n")));
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
  for (const auto& [name, authority] : authorities_) {
    parts.push_back(absl::StrFormat("  %s={\n", name));
    parts.push_back(
        absl::StrFormat("    client_listener_resource_name_template=\"%s\",\n",
                        authority.client_listener_resource_name_template()));
    std::vector<std::string> server_strings;
    for (const XdsServer* server : authority.servers()) {
      server_strings.emplace_back(server->Key());
    }
    if (!server_strings.empty()) {
      parts.push_back(absl::StrFormat("    servers=[\n%s\n],\n",
                                      absl::StrJoin(server_strings, ",\n")));
    }
    parts.push_back("      },\n");
  }
  parts.push_back("}\n");
  parts.push_back("certificate_providers={\n");
  for (const auto& [name, plugin_definition] : certificate_providers_) {
    parts.push_back(
        absl::StrFormat("  %s={\n"
                        "    plugin_name=%s\n"
                        "    config=%s\n"
                        "  },\n",
                        name, plugin_definition.plugin_name,
                        plugin_definition.config->ToString()));
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

}  // namespace grpc_core
