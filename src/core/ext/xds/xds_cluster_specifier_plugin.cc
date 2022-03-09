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

absl::StatusOr<std::string>
XdsRouteLookupClusterSpecifierPlugin::GenerateLoadBalancingPolicyConfig(
    upb_StringView serialized_plugin_config, upb_Arena* arena) const {
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
  Json::Object result;
  // parse array of grpc keybuilders
  size_t num_keybuilders;
  Json::Array keybuilders_array_result;
  const grpc_lookup_v1_GrpcKeyBuilder* const* keybuilders =
      grpc_lookup_v1_RouteLookupConfig_grpc_keybuilders(plugin_config,
                                                        &num_keybuilders);
  for (size_t i = 0; i < num_keybuilders; ++i) {
    Json::Object builder_result;
    // parse array of names
    size_t num_names;
    Json::Array keybuilder_names_array_result;
    const grpc_lookup_v1_GrpcKeyBuilder_Name* const* names =
        grpc_lookup_v1_GrpcKeyBuilder_names(keybuilders[i], &num_names);
    for (size_t j = 0; j < num_names; ++j) {
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
      Json::Object header_result;
      header_result["key"] =
          UpbStringToStdString(grpc_lookup_v1_NameMatcher_key(headers[k]));
      size_t num_header_names;
      Json::Array header_names_result;
      upb_StringView const* header_names =
          grpc_lookup_v1_NameMatcher_names(headers[k], &num_header_names);
      for (size_t l = 0; l < num_header_names; ++l) {
        header_names_result.emplace_back(UpbStringToStdString(header_names[l]));
      }
      header_result["names"] = std::move(header_names_result);
      header_result["required_match"] =
          grpc_lookup_v1_NameMatcher_required_match(headers[k]);
      keybuilder_headers_array_result.emplace_back(std::move(header_result));
    }
    builder_result["headers"] = std::move(keybuilder_headers_array_result);
    // parse constant keys
    Json::Object const_keys_map_result;
    size_t const_key_it = kUpb_Map_Begin;
    while (true) {
      const auto* const_key_entry =
          grpc_lookup_v1_GrpcKeyBuilder_constant_keys_next(keybuilders[i],
                                                           &const_key_it);
      if (const_key_entry == nullptr) break;
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
  // parse lookupService
  result["lookupService"] = UpbStringToStdString(
      grpc_lookup_v1_RouteLookupConfig_lookup_service(plugin_config));
  // parse lookupServiceTimeout
  if (grpc_lookup_v1_RouteLookupConfig_has_lookup_service_timeout(
          plugin_config)) {
    const auto* timeout =
        grpc_lookup_v1_RouteLookupConfig_lookup_service_timeout(plugin_config);
    result["lookupServiceTimeout"] =
        absl::StrFormat("%d.%09ds", google_protobuf_Duration_seconds(timeout),
                        google_protobuf_Duration_nanos(timeout));
  }
  // parse maxAge
  if (grpc_lookup_v1_RouteLookupConfig_has_max_age(plugin_config)) {
    const auto* max_age =
        grpc_lookup_v1_RouteLookupConfig_max_age(plugin_config);
    result["maxAge"] =
        absl::StrFormat("%d.%09ds", google_protobuf_Duration_seconds(max_age),
                        google_protobuf_Duration_nanos(max_age));
  }
  // parse staleAge
  if (grpc_lookup_v1_RouteLookupConfig_has_stale_age(plugin_config)) {
    const auto* stale_age =
        grpc_lookup_v1_RouteLookupConfig_stale_age(plugin_config);
    result["staleAge"] =
        absl::StrFormat("%d.%09ds", google_protobuf_Duration_seconds(stale_age),
                        google_protobuf_Duration_nanos(stale_age));
  }
  // parse cashSizeBytes
  result["cashSizeBytes"] =
      grpc_lookup_v1_RouteLookupConfig_cache_size_bytes(plugin_config);
  // parse defaultTarget
  result["defaultTarget"] = UpbStringToStdString(
      grpc_lookup_v1_RouteLookupConfig_default_target(plugin_config));
  // Construct the config
  Json::Object rls_policy;
  rls_policy["routeLookupConfig"] = std::move(result);
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
  gpr_log(GPR_INFO, "donna rls policy dump %s", Json(policies).Dump().c_str());
  return Json(policies).Dump();
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
    upb_Arena* arena) {
  auto it = g_plugin_registry->find(proto_type_name);
  if (it == g_plugin_registry->end()) {
    return absl::InvalidArgumentError(
        "Unable to locate the cluster specifier plugin in the registry");
  }
  return it->second->GenerateLoadBalancingPolicyConfig(serialized_plugin_config,
                                                       arena);
}

void XdsClusterSpecifierPluginRegistry::Init() {
  g_plugin_registry = new PluginRegistryMap;
  RegisterPlugin(absl::make_unique<XdsRouteLookupClusterSpecifierPlugin>(),
                 kXdsRouteLookupClusterSpecifierPluginConfigName);
}

void XdsClusterSpecifierPluginRegistry::Shutdown() { delete g_plugin_registry; }

}  // namespace grpc_core
