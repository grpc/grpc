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

#include "src/core/util/down_cast.h"
#include "src/core/util/env.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// TODO(roth): Remove this once the feature passes interop tests.
bool XdsExtProcOnClientEnabled() {
  auto value = GetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT");
  if (!value.has_value()) return false;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

//
// GrpcXdsBootstrap::GrpcNode::Locality
//

std::string GrpcXdsBootstrap::GrpcNode::Locality::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (!region.empty()) {
    StrAppend(result, "region=");
    StrAppend(result, region);
    is_first = false;
  }
  if (!zone.empty()) {
    StrAppend(result, is_first ? "zone=" : ", zone=");
    StrAppend(result, zone);
    is_first = false;
  }
  if (!sub_zone.empty()) {
    StrAppend(result, is_first ? "sub_zone=" : ", sub_zone=");
    StrAppend(result, sub_zone);
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

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

std::string GrpcXdsBootstrap::GrpcNode::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (!id_.empty()) {
    StrAppend(result, "id=");
    StrAppend(result, id_);
    is_first = false;
  }
  if (!cluster_.empty()) {
    StrAppend(result, is_first ? "cluster=" : ", cluster=");
    StrAppend(result, cluster_);
    is_first = false;
  }
  if (!locality_.Empty()) {
    StrAppend(result, is_first ? "locality=" : ", locality=");
    StrAppend(result, locality_.ToString());
    is_first = false;
  }
  if (!metadata_.empty()) {
    StrAppend(result, is_first ? "metadata=" : ", metadata=");
    StrAppend(result, JsonDump(Json::FromObject(metadata_)));
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

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

std::string GrpcXdsBootstrap::GrpcAuthority::ToString() const {
  std::string result = "{";
  bool is_first = false;
  if (!client_listener_resource_name_template_.empty()) {
    StrAppend(result, "client_listener_resource_name_template=\"");
    StrAppend(result, client_listener_resource_name_template_);
    StrAppend(result, "\"");
    is_first = false;
  }
  if (!servers_.empty()) {
    StrAppend(result, is_first ? "servers=[" : ", servers=[");
    bool is_first_server = true;
    for (const auto& server : servers_) {
      if (!is_first_server) StrAppend(result, ", ");
      StrAppend(result, server.Key());
      is_first_server = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

const JsonLoaderInterface* GrpcXdsBootstrap::GrpcAuthority::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcAuthority>()
          .OptionalField(
              "client_listener_resource_name_template",
              &GrpcAuthority::client_listener_resource_name_template_)
          .OptionalField("xds_servers", &GrpcAuthority::servers_)
          .OptionalField("fallback_on_reachability_only",
                         &GrpcAuthority::fallback_on_reachability_only_)
          .Finish();
  return loader;
}

//
// GrpcXdsBootstrap::AllowedGrpcService
//

std::string GrpcXdsBootstrap::AllowedGrpcService::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (channel_creds_config != nullptr) {
    StrAppend(result, "channel_creds={type=");
    StrAppend(result, channel_creds_config->type());
    StrAppend(result, ", config=");
    StrAppend(result, channel_creds_config->ToString());
    StrAppend(result, "}");
    is_first = false;
  }
  for (const auto& call_creds_config : call_creds_configs) {
    StrAppend(result, is_first ? "call_creds={type=" : ", call_creds={type=");
    StrAppend(result, call_creds_config->type());
    StrAppend(result, ", config=");
    StrAppend(result, call_creds_config->ToString());
    StrAppend(result, "}");
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

const JsonLoaderInterface* GrpcXdsBootstrap::AllowedGrpcService::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<AllowedGrpcService>().Finish();
  return loader;
};

void GrpcXdsBootstrap::AllowedGrpcService::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  // Parse "channel_creds".
  channel_creds_config = ParseXdsBootstrapChannelCreds(json, args, errors);
  // Parse "call_creds".
  call_creds_configs = ParseXdsBootstrapCallCreds(json, args, errors);
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
      if (key == "grpc_service") return XdsExtProcOnClientEnabled();
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
          .OptionalField("allowed_grpc_services",
                         &GrpcXdsBootstrap::allowed_grpc_services_,
                         "grpc_service")
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
  std::string result = "{servers=[";
  bool is_first_server = false;
  for (const auto& server : servers_) {
    if (!is_first_server) StrAppend(result, ", ");
    StrAppend(result, server.Key());
    is_first_server = false;
  }
  StrAppend(result, "]");
  if (node_.has_value()) {
    StrAppend(result, ",\n  node=");
    StrAppend(result, node_->ToString());
  }
  if (!client_default_listener_resource_name_template_.empty()) {
    StrAppend(result, ",\n  client_default_listener_resource_name_template=\"");
    StrAppend(result, client_default_listener_resource_name_template_);
    StrAppend(result, "\"");
  }
  if (!server_listener_resource_name_template_.empty()) {
    StrAppend(result, ",\n  server_listener_resource_name_template=\"");
    StrAppend(result, server_listener_resource_name_template_);
    StrAppend(result, "\"");
  }
  if (!authorities_.empty()) {
    StrAppend(result, ",\n  authorities={");
    bool is_first_authority = true;
    for (const auto& [name, authority] : authorities_) {
      StrAppend(result, is_first_authority ? ",\n    " : "\n    ");
      StrAppend(result, name);
      StrAppend(result, "=");
      StrAppend(result, authority.ToString());
      is_first_authority = false;
    }
    StrAppend(result, "\n  }");
  }
  if (!certificate_providers_.empty()) {
    StrAppend(result, ",\n  certificate_providers={");
    bool is_first_provider = true;
    for (const auto& [name, plugin_definition] : certificate_providers_) {
      StrAppend(result, is_first_provider ? ",\n    " : "\n    ");
      StrAppend(result, name);
      StrAppend(result, "={plugin_name=");
      StrAppend(result, plugin_definition.plugin_name);
      StrAppend(result, ", config=");
      StrAppend(result, plugin_definition.config->ToString());
      StrAppend(result, "}");
      is_first_provider = false;
    }
    StrAppend(result, "\n  }");
  }
  if (!allowed_grpc_services_.empty()) {
    StrAppend(result, ",\n  allowed_grpc_services={");
    bool is_first_grpc_service = true;
    for (const auto& [target_uri, creds] : allowed_grpc_services_) {
      StrAppend(result, is_first_grpc_service ? ",\n    " : "\n    ");
      StrAppend(result, target_uri);
      StrAppend(result, "=");
      StrAppend(result, creds.ToString());
      is_first_grpc_service = false;
    }
    StrAppend(result, "\n  }");
  }
  StrAppend(result, "\n}");
  return result;
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
