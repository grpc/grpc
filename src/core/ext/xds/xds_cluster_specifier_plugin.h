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

#ifndef GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_H
#define GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <set>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.upb.h"
#include "upb/def.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

class XdsClusterSpecifierPluginImpl {
 public:
  virtual ~XdsClusterSpecifierPluginImpl() = default;

  // Returns the LB policy config in JSON form.
  static absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      absl::string_view proto_type_name, upb_strview serialized_plugin_config,
      upb_arena* arena);

  /*

  // Loads the proto message into the upb symtab.
  virtual void PopulateSymtab(upb_symtab* symtab) const = 0;

  // Generates a Config from the xDS plugin config proto.
  // Used for the top-level config in the HCM HTTP plugin list.
  virtual absl::StatusOr<PluginConfig> GeneratePluginConfig(
      upb_strview serialized_plugin_config, upb_arena* arena) const = 0;

  // Generates a Config from the xDS plugin config proto.
  // Used for the typed_per_plugin_config override in VirtualHost and Route.
  virtual absl::StatusOr<PluginConfig> GeneratePluginConfigOverride(
      upb_strview serialized_plugin_config, upb_arena* arena) const = 0;

  // C-core channel plugin implementation.
  virtual const grpc_channel_plugin* channel_plugin() const = 0;

  // Modifies channel args that may affect service config parsing (not
  // visible to the channel as a whole).
  // Takes ownership of args.  Caller takes ownership of return value.
  virtual grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) const {
    return args;
  }

  // Function to convert the Configs into a JSON string to be added to the
  // per-method part of the service config.
  // The hcm_plugin_config comes from the HttpConnectionManager config.
  // The plugin_config_override comes from the first of the ClusterWeight,
  // Route, or VirtualHost entries that it is found in, or null if
  // there is no override in any of those locations.
  virtual absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const PluginConfig& hcm_plugin_config,
      const PluginConfig* plugin_config_override) const = 0;

  // Returns true if the plugin is supported on clients; false otherwise
  virtual bool IsSupportedOnClients() const = 0;

  // Returns true if the plugin is supported on servers; false otherwise
  virtual bool IsSupportedOnServers() const = 0;

  // Returns true if the plugin must be the last plugin in the chain.
  virtual bool IsTerminalPlugin() const { return false; }
  */
};

class XdsClusterSpecifierPluginRegistry {
 public:
  static void RegisterPlugin(
      std::unique_ptr<XdsClusterSpecifierPluginImpl> plugin,
      const std::set<absl::string_view>& config_proto_type_names);

  static const XdsClusterSpecifierPluginImpl* GetPluginForType(
      absl::string_view proto_type_name);

  static void PopulateSymtab(upb_symtab* symtab);

  // Global init and shutdown.
  static void Init();
  static void Shutdown();
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_H */
