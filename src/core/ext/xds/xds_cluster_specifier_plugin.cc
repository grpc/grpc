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

namespace grpc_core {

const char* kXdRouteLookupClusterSpecifierPluginConfigName =
    "type.googleapis.com/grpc.lookup.v1.RouteLookupClusterSpecifier";

namespace {

class XdsRouteLookupClusterSpecifierPlugin : public XdsClusterSpecifierPluginImpl {
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

using PluginOwnerList = std::vector<std::unique_ptr<XdsClusterSpecifierPluginImpl>>;
using PluginRegistryMap = std::map<absl::string_view, XdsClusterSpecifierPluginImpl*>;

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

const XdsClusterSpecifierPluginImpl* XdsClusterSpecifierPluginRegistry::GetPluginForType(
    absl::string_view proto_type_name) {
  auto it = g_plugin_registry->find(proto_type_name);
  if (it == g_plugin_registry->end()) return nullptr;
  return it->second;
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
