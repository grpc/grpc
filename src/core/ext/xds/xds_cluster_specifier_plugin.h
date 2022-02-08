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
  virtual absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      upb_strview serialized_plugin_config, upb_arena* arena) const = 0;

  // Loads the proto message into the upb symtab.
  virtual void PopulateSymtab(upb_symtab* symtab) const = 0;
};

class XdsRouteLookupClusterSpecifierPlugin
    : public XdsClusterSpecifierPluginImpl {
  // Overrides the PopulateSymtab method
  void PopulateSymtab(upb_symtab* symtab) const override;

  absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      upb_strview serialized_plugin_config, upb_arena* arena) const override;
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

#endif  // GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_H
