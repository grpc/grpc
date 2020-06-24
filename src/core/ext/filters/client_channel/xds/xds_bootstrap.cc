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

#include "src/core/ext/filters/client_channel/xds/xds_bootstrap.h"

#include <vector>

#include <errno.h>
#include <stdlib.h>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

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
  parts.push_back(
      absl::StrFormat("servers=[\n"
                      "  {\n"
                      "    uri=\"%s\",\n"
                      "    creds=[\n",
                      bootstrap.server().server_uri));
  for (const auto& creds : bootstrap.server().channel_creds) {
    parts.push_back(absl::StrFormat("      {type=\"%s\", config=%s},\n",
                                    creds.type, creds.config.Dump()));
  }
  parts.push_back("    ]\n  }\n]");
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
    char* msg;
    gpr_asprintf(&msg, "Failed to parse bootstrap file %s", path.get());
    grpc_error* error_out =
        GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(msg, error, 1);
    gpr_free(msg);
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
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing xds bootstrap file",
                                         &error_list);
}

grpc_error* XdsBootstrap::ParseXdsServerList(Json* json) {
  std::vector<grpc_error*> error_list;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::OBJECT) {
      char* msg;
      gpr_asprintf(&msg, "array element %" PRIuPTR " is not an object", i);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
      gpr_free(msg);
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
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"channel_creds\" field is not an array"));
    } else {
      grpc_error* parse_error = ParseChannelCredsArray(&it->second, &server);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  char* msg;
  gpr_asprintf(&msg, "errors parsing index %" PRIuPTR, idx);
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  gpr_free(msg);
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
      char* msg;
      gpr_asprintf(&msg, "array element %" PRIuPTR " is not an object", i);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
      gpr_free(msg);
    } else {
      grpc_error* parse_error = ParseChannelCreds(&child, i, server);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"channel_creds\" array",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseChannelCreds(Json* json, size_t idx,
                                            XdsServer* server) {
  std::vector<grpc_error*> error_list;
  ChannelCreds channel_creds;
  auto it = json->mutable_object()->find("type");
  if (it == json->mutable_object()->end()) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"type\" field not present"));
  } else if (it->second.type() != Json::Type::STRING) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"type\" field is not a string"));
  } else {
    channel_creds.type = std::move(*it->second.mutable_string_value());
  }
  it = json->mutable_object()->find("config");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"config\" field is not an object"));
    } else {
      channel_creds.config = std::move(it->second);
    }
  }
  if (!channel_creds.type.empty()) {
    server->channel_creds.emplace_back(std::move(channel_creds));
  }
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  char* msg;
  gpr_asprintf(&msg, "errors parsing index %" PRIuPTR, idx);
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  gpr_free(msg);
  for (size_t i = 0; i < error_list.size(); ++i) {
    error = grpc_error_add_child(error, error_list[i]);
  }
  return error;
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

}  // namespace grpc_core
