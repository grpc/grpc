/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/xds/xds_channel.h"

namespace grpc_core {

grpc_error* ParseTargetUri(envoy_api_v2_ConfigSource* config_source, std::string* target_uri) {
  if (!envoy_api_v2_ConfigSource_has_api_config_source(config_source)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "config source does not have api_config_source field.");
  }
  envoy_api_v2_ApiConfigSource* api_config_source = envoy_api_v2_ConfigSource_api_config_source(config_source);
  size_t grpc_services_size;
  envoy_api_v2_GrpcService* const* grpc_service = envoy_api_v2_ApiConfigSource_grpc_services(api_config_source, &size);
  if (size == 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "api config source does not have a grpc_services config.");
  }
  // For now, support only using the first grpc service config
  if (!evnoy_api_v2_GoogleGrpc_has_google_grpc(grpc_service)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "grpc service config does not have a google_grpc config.");
  }
}

grpc_channel_args* ModifyXdsChannelArgs(grpc_channel_args* args) {
  return args;
}

grpc_channel* CreateXdsChannel(const XdsBootstrap& bootstrap,
                               const grpc_channel_args& args,
                               grpc_error** error) {
  if (!bootstrap.server().channel_creds.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "credential specified but gRPC not built with security");
    return nullptr;
  }
  return grpc_insecure_channel_create(bootstrap.server().server_uri.c_str(),
                                      &args, nullptr);
}

grpc_channel* CreateSdsChannel(envoy_api_v2_ConfigSource* config_source,
                               const grpc_channel_args& args,
                               grpc_error** error) {
  std::string parsed_target_uri;
  *error = XdsParseConfigSource(config_source, &parsed_target_uri);
  if (*error != GRPC_ERROR_NONE) {
    return nullptr;
  }
  return grpc_insecure_channel_create(parsed_target_uri.c_str(), &args, nullptr);
}

}  // namespace grpc_core
