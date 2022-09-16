//
// Copyright 2022 gRPC authors.
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

#include <stddef.h>

#include <algorithm>
#include <map>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "upb/json_encode.h"
#include "upb/status.h"
#include "upb/upb.hpp"

#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/proto/grpc/lookup/v1/rls_config.upb.h"
#include "src/proto/grpc/lookup/v1/rls_config.upbdefs.h"

namespace grpc_core {

const char* kXdsRouteLookupClusterSpecifierPluginConfigName =
    "grpc.lookup.v1.RouteLookupClusterSpecifier";

void XdsRouteLookupClusterSpecifierPlugin::PopulateSymtab(
    upb_DefPool* symtab) const {
  grpc_lookup_v1_RouteLookupConfig_getmsgdef(symtab);
}

absl::StatusOr<std::string>
XdsRouteLookupClusterSpecifierPlugin::GenerateLoadBalancingPolicyConfig(
    upb_StringView serialized_plugin_config, upb_Arena* arena,
    upb_DefPool* symtab) const {
  const auto* specifier = grpc_lookup_v1_RouteLookupClusterSpecifier_parse(
      serialized_plugin_config.data, serialized_plugin_config.size, arena);
  if (specifier == nullptr) {
    return absl::InvalidArgumentError("Could not parse plugin config");
  }
  const auto* plugin_config =
      grpc_lookup_v1_RouteLookupClusterSpecifier_route_lookup_config(specifier);
  if (plugin_config == nullptr) {
    return absl::InvalidArgumentError(
        "Could not get route lookup config from route lookup cluster "
        "specifier");
  }
  upb::Status status;
  const upb_MessageDef* msg_type =
      grpc_lookup_v1_RouteLookupConfig_getmsgdef(symtab);
  size_t json_size = upb_JsonEncode(plugin_config, msg_type, symtab, 0, nullptr,
                                    0, status.ptr());
  if (json_size == static_cast<size_t>(-1)) {
    return absl::InvalidArgumentError(
        absl::StrCat("failed to dump proto to JSON: ",
                     upb_Status_ErrorMessage(status.ptr())));
  }
  void* buf = upb_Arena_Malloc(arena, json_size + 1);
  upb_JsonEncode(plugin_config, msg_type, symtab, 0,
                 reinterpret_cast<char*>(buf), json_size + 1, status.ptr());
  Json::Object rls_policy;
  auto json = Json::Parse(reinterpret_cast<char*>(buf));
  GPR_ASSERT(json.ok());
  rls_policy["routeLookupConfig"] = std::move(*json);
  Json::Object cds_policy;
  cds_policy["cds_experimental"] = Json::Object();
  Json::Array child_policy;
  child_policy.emplace_back(std::move(cds_policy));
  rls_policy["childPolicy"] = std::move(child_policy);
  rls_policy["childPolicyConfigTargetFieldName"] = "cluster";
  Json::Object policy;
  policy["rls_experimental"] = std::move(rls_policy);
  Json::Array policies;
  policies.emplace_back(std::move(policy));
  Json lb_policy_config(std::move(policies));
  // TODO(roth): If/when we ever add a second plugin, refactor this code
  // somehow such that we automatically validate the resulting config against
  // the gRPC LB policy registry instead of requiring each plugin to do that
  // itself.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          lb_policy_config);
  if (!config.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        kXdsRouteLookupClusterSpecifierPluginConfigName,
        " ClusterSpecifierPlugin returned invalid LB policy config: ",
        config.status().message()));
  }
  return lb_policy_config.Dump();
}

namespace {

using PluginRegistryMap =
    std::map<absl::string_view, std::unique_ptr<XdsClusterSpecifierPluginImpl>>;

PluginRegistryMap* g_plugin_registry = nullptr;

}  // namespace

const XdsClusterSpecifierPluginImpl*
XdsClusterSpecifierPluginRegistry::GetPluginForType(
    absl::string_view config_proto_type_name) {
  auto it = g_plugin_registry->find(config_proto_type_name);
  if (it == g_plugin_registry->end()) return nullptr;
  return it->second.get();
}

void XdsClusterSpecifierPluginRegistry::PopulateSymtab(upb_DefPool* symtab) {
  for (const auto& p : *g_plugin_registry) {
    p.second->PopulateSymtab(symtab);
  }
}

void XdsClusterSpecifierPluginRegistry::RegisterPlugin(
    std::unique_ptr<XdsClusterSpecifierPluginImpl> plugin,
    absl::string_view config_proto_type_name) {
  (*g_plugin_registry)[config_proto_type_name] = std::move(plugin);
}

void XdsClusterSpecifierPluginRegistry::Init() {
  g_plugin_registry = new PluginRegistryMap;
  RegisterPlugin(absl::make_unique<XdsRouteLookupClusterSpecifierPlugin>(),
                 kXdsRouteLookupClusterSpecifierPluginConfigName);
}

void XdsClusterSpecifierPluginRegistry::Shutdown() { delete g_plugin_registry; }

}  // namespace grpc_core
