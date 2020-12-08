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

#include <vector>

#include <errno.h>
#include <stdlib.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

//
// XdsChannelCredsRegistry
//

bool XdsChannelCredsRegistry::IsSupported(const std::string& creds_type) {
  return creds_type == "google_default" || creds_type == "insecure" ||
         creds_type == "fake";
}

bool XdsChannelCredsRegistry::IsValidConfig(const std::string& creds_type,
                                            const Json& config) {
  // Currently, none of the creds types actually take a config, but we
  // ignore whatever might be specified in the bootstrap file for
  // forward compatibility reasons.
  return true;
}

RefCountedPtr<grpc_channel_credentials>
XdsChannelCredsRegistry::MakeChannelCreds(const std::string& creds_type,
                                          const Json& config) {
  if (creds_type == "google_default") {
    return grpc_google_default_credentials_create(nullptr);
  } else if (creds_type == "insecure") {
    return grpc_insecure_credentials_create();
  } else if (creds_type == "fake") {
    return grpc_fake_transport_security_credentials_create();
  }
  return nullptr;
}

//
// XdsBootstrap::XdsServer
//

bool XdsBootstrap::XdsServer::ShouldUseV3() const {
  return server_features.find("xds_v3") != server_features.end();
}

//
// XdsBootstrap
//

namespace {

std::string BootstrapString(const XdsBootstrap& bootstrap) {
  std::vector<std::string> parts;
  if (bootstrap.node() != nullptr) {
    parts.push_back(absl::StrFormat(
        "node={\n"
        "  id=\"%s\",\n"
        "  cluster=\"%s\",\n"
        "  locality={\n"
        "    region=\"%s\",\n"
        "    zone=\"%s\",\n"
        "    subzone=\"%s\"\n"
        "  },\n"
        "  metadata=%s,\n"
        "},\n",
        bootstrap.node()->id, bootstrap.node()->cluster,
        bootstrap.node()->locality_region, bootstrap.node()->locality_zone,
        bootstrap.node()->locality_subzone, bootstrap.node()->metadata.Dump()));
  }
  parts.push_back(absl::StrFormat(
      "servers=[\n"
      "  {\n"
      "    uri=\"%s\",\n"
      "    creds_type=%s,\n",
      bootstrap.server().server_uri, bootstrap.server().channel_creds_type));
  if (bootstrap.server().channel_creds_config.type() != Json::Type::JSON_NULL) {
    parts.push_back(
        absl::StrFormat("    creds_config=%s,",
                        bootstrap.server().channel_creds_config.Dump()));
  }
  if (!bootstrap.server().server_features.empty()) {
    parts.push_back(absl::StrCat(
        "    server_features=[",
        absl::StrJoin(bootstrap.server().server_features, ", "), "],\n"));
  }
  parts.push_back("  }\n],\n");
  parts.push_back("certificate_providers={\n");
  for (const auto& entry : bootstrap.certificate_providers()) {
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

}  // namespace

std::unique_ptr<XdsBootstrap> XdsBootstrap::ReadFromFile(XdsClient* client,
                                                         TraceFlag* tracer,
                                                         grpc_error** error) {
  grpc_core::UniquePtr<char> path(gpr_getenv("GRPC_XDS_BOOTSTRAP"));
  if (path == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Environment variable GRPC_XDS_BOOTSTRAP not defined");
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED(*tracer)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] Got bootstrap file location from "
            "GRPC_XDS_BOOTSTRAP environment variable: %s",
            client, path.get());
  }
  grpc_slice contents;
  *error = grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
  if (*error != GRPC_ERROR_NONE) return nullptr;
  absl::string_view contents_str_view = StringViewFromSlice(contents);
  if (GRPC_TRACE_FLAG_ENABLED(*tracer)) {
    gpr_log(GPR_DEBUG, "[xds_client %p] Bootstrap file contents: %s", client,
            std::string(contents_str_view).c_str());
  }
  Json json = Json::Parse(contents_str_view, error);
  grpc_slice_unref_internal(contents);
  if (*error != GRPC_ERROR_NONE) {
    grpc_error* error_out = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
        absl::StrCat("Failed to parse bootstrap file ", path.get()).c_str(),
        error, 1);
    GRPC_ERROR_UNREF(*error);
    *error = error_out;
    return nullptr;
  }
  std::unique_ptr<XdsBootstrap> result =
      absl::make_unique<XdsBootstrap>(std::move(json), error);
  if (*error == GRPC_ERROR_NONE && GRPC_TRACE_FLAG_ENABLED(*tracer)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] Bootstrap config for creating xds client:\n%s",
            client, BootstrapString(*result).c_str());
  }
  return result;
}

XdsBootstrap::XdsBootstrap(Json json, grpc_error** error) {
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "malformed JSON in bootstrap file");
    return;
  }
  std::vector<grpc_error*> error_list;
  auto it = json.mutable_object()->find("xds_servers");
  if (it == json.mutable_object()->end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"xds_servers\" field not present"));
  } else if (it->second.type() != Json::Type::ARRAY) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"xds_servers\" field is not an array"));
  } else {
    grpc_error* parse_error = ParseXdsServerList(&it->second);
    if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
  }
  it = json.mutable_object()->find("node");
  if (it != json.mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"node\" field is not an object"));
    } else {
      grpc_error* parse_error = ParseNode(&it->second);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  if (XdsSecurityEnabled()) {
    it = json.mutable_object()->find("certificate_providers");
    if (it != json.mutable_object()->end()) {
      if (it->second.type() != Json::Type::OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"certificate_providers\" field is not an object"));
      } else {
        grpc_error* parse_error = ParseCertificateProviders(&it->second);
        if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
      }
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing xds bootstrap file",
                                         &error_list);
}

grpc_error* XdsBootstrap::ParseXdsServerList(Json* json) {
  std::vector<grpc_error*> error_list;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("array element ", i, " is not an object").c_str()));
    } else {
      grpc_error* parse_error = ParseXdsServer(&child, i);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"xds_servers\" array",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseXdsServer(Json* json, size_t idx) {
  std::vector<grpc_error*> error_list;
  servers_.emplace_back();
  XdsServer& server = servers_[servers_.size() - 1];
  auto it = json->mutable_object()->find("server_uri");
  if (it == json->mutable_object()->end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"server_uri\" field not present"));
  } else if (it->second.type() != Json::Type::STRING) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"server_uri\" field is not a string"));
  } else {
    server.server_uri = std::move(*it->second.mutable_string_value());
  }
  it = json->mutable_object()->find("channel_creds");
  if (it == json->mutable_object()->end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"channel_creds\" field not present"));
  } else if (it->second.type() != Json::Type::ARRAY) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"channel_creds\" field is not an array"));
  } else {
    grpc_error* parse_error = ParseChannelCredsArray(&it->second, &server);
    if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
  }
  it = json->mutable_object()->find("server_features");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"server_features\" field is not an array"));
    } else {
      grpc_error* parse_error = ParseServerFeaturesArray(&it->second, &server);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      absl::StrCat("errors parsing index ", idx).c_str());
  for (size_t i = 0; i < error_list.size(); ++i) {
    error = grpc_error_add_child(error, error_list[i]);
  }
  return error;
}

grpc_error* XdsBootstrap::ParseChannelCredsArray(Json* json,
                                                 XdsServer* server) {
  std::vector<grpc_error*> error_list;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("array element ", i, " is not an object").c_str()));
    } else {
      grpc_error* parse_error = ParseChannelCreds(&child, i, server);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  if (server->channel_creds_type.empty()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "no known creds type found in \"channel_creds\""));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"channel_creds\" array",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseChannelCreds(Json* json, size_t idx,
                                            XdsServer* server) {
  std::vector<grpc_error*> error_list;
  std::string type;
  auto it = json->mutable_object()->find("type");
  if (it == json->mutable_object()->end()) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"type\" field not present"));
  } else if (it->second.type() != Json::Type::STRING) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"type\" field is not a string"));
  } else {
    type = std::move(*it->second.mutable_string_value());
  }
  Json config;
  it = json->mutable_object()->find("config");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"config\" field is not an object"));
    } else {
      config = std::move(it->second);
    }
  }
  // Select the first channel creds type that we support.
  if (server->channel_creds_type.empty() &&
      XdsChannelCredsRegistry::IsSupported(type)) {
    if (!XdsChannelCredsRegistry::IsValidConfig(type, config)) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("invalid config for channel creds type \"", type, "\"")
              .c_str()));
    }
    server->channel_creds_type = std::move(type);
    server->channel_creds_config = std::move(config);
  }
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      absl::StrCat("errors parsing index ", idx).c_str());
  for (size_t i = 0; i < error_list.size(); ++i) {
    error = grpc_error_add_child(error, error_list[i]);
  }
  return error;
}

grpc_error* XdsBootstrap::ParseServerFeaturesArray(Json* json,
                                                   XdsServer* server) {
  std::vector<grpc_error*> error_list;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() == Json::Type::STRING &&
        child.string_value() == "xds_v3") {
      // TODO(roth): Remove env var check once we do interop testing and
      // are sure that the v3 code actually works.
      grpc_core::UniquePtr<char> enable_str(
          gpr_getenv("GRPC_XDS_EXPERIMENTAL_V3_SUPPORT"));
      bool enabled = false;
      if (gpr_parse_bool_value(enable_str.get(), &enabled) && enabled) {
        server->server_features.insert(
            std::move(*child.mutable_string_value()));
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing \"server_features\" array", &error_list);
}

grpc_error* XdsBootstrap::ParseNode(Json* json) {
  std::vector<grpc_error*> error_list;
  node_ = absl::make_unique<Node>();
  auto it = json->mutable_object()->find("id");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"id\" field is not a string"));
    } else {
      node_->id = std::move(*it->second.mutable_string_value());
    }
  }
  it = json->mutable_object()->find("cluster");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"cluster\" field is not a string"));
    } else {
      node_->cluster = std::move(*it->second.mutable_string_value());
    }
  }
  it = json->mutable_object()->find("locality");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"locality\" field is not an object"));
    } else {
      grpc_error* parse_error = ParseLocality(&it->second);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  it = json->mutable_object()->find("metadata");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"metadata\" field is not an object"));
    } else {
      node_->metadata = std::move(it->second);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"node\" object",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseLocality(Json* json) {
  std::vector<grpc_error*> error_list;
  auto it = json->mutable_object()->find("region");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"region\" field is not a string"));
    } else {
      node_->locality_region = std::move(*it->second.mutable_string_value());
    }
  }
  it = json->mutable_object()->find("zone");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"zone\" field is not a string"));
    } else {
      node_->locality_zone = std::move(*it->second.mutable_string_value());
    }
  }
  it = json->mutable_object()->find("subzone");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"subzone\" field is not a string"));
    } else {
      node_->locality_subzone = std::move(*it->second.mutable_string_value());
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"locality\" object",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseCertificateProviders(Json* json) {
  std::vector<grpc_error*> error_list;
  for (auto& certificate_provider : *(json->mutable_object())) {
    if (certificate_provider.second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("element \"", certificate_provider.first,
                       "\" is not an object")
              .c_str()));
    } else {
      grpc_error* parse_error = ParseCertificateProvider(
          certificate_provider.first, &certificate_provider.second);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing \"certificate_providers\" object", &error_list);
}

grpc_error* XdsBootstrap::ParseCertificateProvider(
    const std::string& instance_name, Json* certificate_provider_json) {
  std::vector<grpc_error*> error_list;
  auto it = certificate_provider_json->mutable_object()->find("plugin_name");
  if (it == certificate_provider_json->mutable_object()->end()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"plugin_name\" field not present"));
  } else if (it->second.type() != Json::Type::STRING) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"plugin_name\" field is not a string"));
  } else {
    std::string plugin_name = std::move(*(it->second.mutable_string_value()));
    CertificateProviderFactory* factory =
        CertificateProviderRegistry::LookupCertificateProviderFactory(
            plugin_name);
    if (factory != nullptr) {
      RefCountedPtr<CertificateProviderFactory::Config> config;
      it = certificate_provider_json->mutable_object()->find("config");
      if (it != certificate_provider_json->mutable_object()->end()) {
        if (it->second.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "\"config\" field is not an object"));
        } else {
          grpc_error* parse_error = GRPC_ERROR_NONE;
          config = factory->CreateCertificateProviderConfig(it->second,
                                                            &parse_error);
          if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
        }
      } else {
        // "config" is an optional field, so create an empty JSON object.
        grpc_error* parse_error = GRPC_ERROR_NONE;
        config = factory->CreateCertificateProviderConfig(Json::Object(),
                                                          &parse_error);
        if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
      }
      certificate_providers_.insert(
          {instance_name, {std::move(plugin_name), std::move(config)}});
    }
  }
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
      absl::StrCat("errors parsing element \"", instance_name, "\"").c_str());
  for (size_t i = 0; i < error_list.size(); ++i) {
    error = grpc_error_add_child(error, error_list[i]);
  }
  return error;
}

}  // namespace grpc_core
