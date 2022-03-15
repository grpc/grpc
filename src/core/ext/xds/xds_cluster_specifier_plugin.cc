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

#include "absl/strings/str_format.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"
#include "google/protobuf/duration.upb.h"
#include "upb/json_encode.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/upb_utils.h"
#include "src/proto/grpc/lookup/v1/rls_config.upb.h"
#include "src/proto/grpc/lookup/v1/rls_config.upbdefs.h"

namespace grpc_core {

const char* kXdsRouteLookupClusterSpecifierPluginConfigName =
    "grpc.lookup.v1.RouteLookupClusterSpecifier";

void XdsRouteLookupClusterSpecifierPlugin::PopulateSymtab(
    upb_DefPool* symtab) const {
  grpc_lookup_v1_RouteLookupConfig_getmsgdef(symtab);
}

absl::StatusOr<Json>
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
      // grpc_lookup_v1_RouteLookupClusterSpecifier_getmsgdef(symtab);
      grpc_lookup_v1_RouteLookupConfig_getmsgdef(symtab);
  char buf[10240];
  size_t json_size = upb_JsonEncode(plugin_config, msg_type, symtab, 0, nullptr,
                                    0, status.ptr());
  if (json_size < 10240) {
    upb_JsonEncode(plugin_config, msg_type, symtab, 0, buf, json_size + 1,
                   status.ptr());
  } else {
    return absl::InvalidArgumentError(
        "Route lookup config too big to fit in parsing buffer of size 10240");
  }
  Json::Object rls_policy;
  grpc_error_handle error = GRPC_ERROR_NONE;
  rls_policy["routeLookupConfig"] = Json::Parse(buf, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
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
  return Json(policies);
}

namespace {

using PluginRegistryMap =
    std::map<absl::string_view, std::unique_ptr<XdsClusterSpecifierPluginImpl>>;

PluginRegistryMap* g_plugin_registry = nullptr;

}  // namespace

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

absl::StatusOr<std::string>
XdsClusterSpecifierPluginRegistry::GenerateLoadBalancingPolicyConfig(
    absl::string_view proto_type_name, upb_StringView serialized_plugin_config,
    upb_Arena* arena, upb_DefPool* symtab) {
  auto it = g_plugin_registry->find(proto_type_name);
  if (it == g_plugin_registry->end()) {
    return absl::InvalidArgumentError(
        "Unable to locate the cluster specifier plugin in the registry");
  }
  auto lb_policy_config = it->second->GenerateLoadBalancingPolicyConfig(
      serialized_plugin_config, arena, symtab);
  if (!lb_policy_config.ok()) return lb_policy_config.status();
  grpc_error_handle parse_error = GRPC_ERROR_NONE;
  LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(*lb_policy_config,
                                                        &parse_error);
  if (parse_error != GRPC_ERROR_NONE) {
    absl::Status status = absl::InvalidArgumentError(absl::StrCat(
        proto_type_name,
        " ClusterSpecifierPlugin returned invalid LB policy config: ",
        grpc_error_std_string(parse_error)));
    GRPC_ERROR_UNREF(parse_error);
    return status;
  }
  return lb_policy_config->Dump();
}

void XdsClusterSpecifierPluginRegistry::Init() {
  g_plugin_registry = new PluginRegistryMap;
  RegisterPlugin(absl::make_unique<XdsRouteLookupClusterSpecifierPlugin>(),
                 kXdsRouteLookupClusterSpecifierPluginConfigName);
}

void XdsClusterSpecifierPluginRegistry::Shutdown() { delete g_plugin_registry; }

}  // namespace grpc_core
