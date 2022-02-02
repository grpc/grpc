//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/xds/xds_cluster_specifier_plugin.h"

#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

#include "src/core/ext/xds/upb_utils.h"
#include "src/proto/grpc/lookup/v1/rls_config.upb.h"

namespace grpc_core {

const char* kXdRouteLookupClusterSpecifierPluginConfigName =
    "type.googleapis.com/grpc.lookup.v1.RouteLookupClusterSpecifier";

namespace {

class XdsRouteLookupClusterSpecifierPlugin
    : public XdsClusterSpecifierPluginImpl {
 public:
  /*
  void PopulateSymtab(upb_symtab* symtab) const override {
    envoy_extensions_plugins_http_router_v3_Router_getmsgdef(symtab);
  }

  absl::StatusOr<PluginConfig> GeneratePluginConfig(
      upb_strview serialized_plugin_config, upb_arena* arena) const override {
    if (envoy_extensions_plugins_http_router_v3_Router_parse(
            serialized_plugin_config.data, serialized_plugin_config.size,
            arena) == nullptr) {
      return absl::InvalidArgumentError("could not parse router plugin config");
    }
    return PluginConfig{kXdsHttpRouterPluginConfigName, Json()};
  }

  absl::StatusOr<PluginConfig> GeneratePluginConfigOverride(
      upb_strview /*serialized_plugin_config/,
      upb_arena* /*arena/) const override {
    return absl::InvalidArgumentError(
        "router plugin does not support config override");
  }

  const grpc_channel_plugin* channel_plugin() const override { return nullptr; }

  // No-op.  This will never be called, since channel_plugin() returns null.
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const PluginConfig& /*hcm_plugin_config/,
      const PluginConfig* /*plugin_config_override/) const override {
    return absl::UnimplementedError("router plugin should never be called");
  }

  bool IsSupportedOnClients() const override { return true; }

  bool IsSupportedOnServers() const override { return true; }

  bool IsTerminalPlugin() const override { return true; }*/
};

using PluginOwnerList =
    std::vector<std::unique_ptr<XdsClusterSpecifierPluginImpl>>;
using PluginRegistryMap =
    std::map<absl::string_view, XdsClusterSpecifierPluginImpl*>;

PluginOwnerList* g_plugins = nullptr;
PluginRegistryMap* g_plugin_registry = nullptr;

}  // namespace

void XdsClusterSpecifierPluginRegistry::RegisterPlugin(
    std::unique_ptr<XdsClusterSpecifierPluginImpl> plugin,
    const std::set<absl::string_view>& config_proto_type_names) {
  for (auto config_proto_type_name : config_proto_type_names) {
    (*g_plugin_registry)[config_proto_type_name] = plugin.get();
  }
  g_plugins->push_back(std::move(plugin));
}

const XdsClusterSpecifierPluginImpl*
XdsClusterSpecifierPluginRegistry::GetPluginForType(
    absl::string_view proto_type_name) {
  auto it = g_plugin_registry->find(proto_type_name);
  if (it == g_plugin_registry->end()) return nullptr;
  return it->second;
}

absl::StatusOr<std::string>
XdsClusterSpecifierPluginImpl::GenerateLoadBalancingPolicyConfig(
    absl::string_view proto_type_name, upb_strview serialized_plugin_config,
    upb_arena* arena) {
  const auto* plugin_config = grpc_lookup_v1_RouteLookupConfig_parse(
      serialized_plugin_config.data, serialized_plugin_config.size, arena);
  if (plugin_config == nullptr) {
    return absl::InvalidArgumentError("Could not parse plugin config");
  }
  Json::Object result;
  size_t num_keybuilders;
  Json::Array keybuilders_array_result;
  const grpc_lookup_v1_GrpcKeyBuilder* const* keybuilders =
      grpc_lookup_v1_RouteLookupConfig_grpc_keybuilders(plugin_config,
                                                        &num_keybuilders);
  for (size_t i = 0; i < num_keybuilders; ++i) {
    gpr_log(GPR_INFO, "donna for each key builder");
    Json::Object builder_result;
    // parse array of names
    size_t num_names;
    Json::Array keybuilder_names_array_result;
    const grpc_lookup_v1_GrpcKeyBuilder_Name* const* names =
        grpc_lookup_v1_GrpcKeyBuilder_names(keybuilders[i], &num_names);
    for (size_t j = 0; j < num_names; ++j) {
      gpr_log(GPR_INFO, "donna for each key builder name");
      Json::Object name_result;
      name_result["service"] = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_Name_service(names[j]));
      name_result["method"] = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_Name_method(names[j]));
      keybuilder_names_array_result.emplace_back(std::move(name_result));
    }
    builder_result["names"] = std::move(keybuilder_names_array_result);
    // parse extra_keys
    if (grpc_lookup_v1_GrpcKeyBuilder_has_extra_keys(keybuilders[i])) {
      const auto* extra_keys =
          grpc_lookup_v1_GrpcKeyBuilder_extra_keys(keybuilders[i]);
      Json::Object extra_keys_result;
      extra_keys_result["host"] = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_host(extra_keys));
      extra_keys_result["service"] = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_service(extra_keys));
      extra_keys_result["method"] = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_method(extra_keys));
      builder_result["extra_keys"] = std::move(extra_keys_result);
    }
    // parse headers
    size_t num_headers;
    Json::Array keybuilder_headers_array_result;
    const grpc_lookup_v1_NameMatcher* const* headers =
        grpc_lookup_v1_GrpcKeyBuilder_headers(keybuilders[i], &num_headers);
    for (size_t k = 0; k < num_headers; ++k) {
      gpr_log(GPR_INFO, "donna for each key builder header");
      Json::Object header_result;
      header_result["key"] =
          UpbStringToStdString(grpc_lookup_v1_NameMatcher_key(headers[k]));
      size_t num_header_names;
      Json::Array header_names_result;
      upb_strview const* header_names =
          grpc_lookup_v1_NameMatcher_names(headers[k], &num_header_names);
      for (size_t l = 0; l < num_header_names; ++l) {
        header_names_result.emplace_back(UpbStringToStdString(header_names[l]));
      }
      header_result["names"] = std::move(header_names_result);
      header_result["required_match"] =
          grpc_lookup_v1_NameMatcher_required_match(headers[k]);
      keybuilder_headers_array_result.emplace_back(std::move(header_result));
    }
    builder_result["header"] = std::move(keybuilder_headers_array_result);
    // parse constant keys
    Json::Object const_keys_map_result;
    size_t const_key_it = UPB_MAP_BEGIN;
    while (true) {
      const auto* const_key_entry =
          grpc_lookup_v1_GrpcKeyBuilder_constant_keys_next(keybuilders[i],
                                                           &const_key_it);
      if (const_key_entry != nullptr) break;
      Json::Object const_key_entry_result;
      std::string key = UpbStringToStdString(
          grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry_key(const_key_entry));
      if (!key.empty()) {
        const_keys_map_result[std::move(key)] = UpbStringToStdString(
            grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry_value(
                const_key_entry));
      }
    }
    builder_result["const_keys"] = std::move(const_keys_map_result);
    keybuilders_array_result.emplace_back(std::move(builder_result));
  }
  result["grpcKeybuilders"] = std::move(keybuilders_array_result);
  return Json(result).Dump();
}

/*void XdsClusterSpecifierPluginRegistry::PopulateSymtab(upb_symtab* symtab) {
  for (const auto& plugin : *g_plugins) {
    plugin->PopulateSymtab(symtab);
  }
}*/

void XdsClusterSpecifierPluginRegistry::Init() {
  g_plugins = new PluginOwnerList;
  g_plugin_registry = new PluginRegistryMap;
  RegisterPlugin(absl::make_unique<XdsRouteLookupClusterSpecifierPlugin>(),
                 {kXdRouteLookupClusterSpecifierPluginConfigName});
}

void XdsClusterSpecifierPluginRegistry::Shutdown() {
  delete g_plugin_registry;
  delete g_plugins;
}

}  // namespace grpc_core
