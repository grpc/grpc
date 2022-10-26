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

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"

#include "src/core/ext/xds/xds_common_types.h"

namespace grpc_core {

class XdsClusterSpecifierPluginImpl {
 public:
  virtual ~XdsClusterSpecifierPluginImpl() = default;

  // Loads the proto message into the upb symtab.
  virtual void PopulateSymtab(upb_DefPool* symtab) const = 0;

  // Returns the LB policy config in JSON form.
  virtual absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      XdsExtension extension, upb_Arena* arena, upb_DefPool* symtab) const = 0;
};

class XdsRouteLookupClusterSpecifierPlugin
    : public XdsClusterSpecifierPluginImpl {
  void PopulateSymtab(upb_DefPool* symtab) const override;

  absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      XdsExtension extension, upb_Arena* arena,
      upb_DefPool* symtab) const override;
};

class XdsClusterSpecifierPluginRegistry {
 public:
  XdsClusterSpecifierPluginRegistry();

  // Not copyable.
  XdsClusterSpecifierPluginRegistry(const XdsClusterSpecifierPluginRegistry&) =
      delete;
  XdsClusterSpecifierPluginRegistry& operator=(
      const XdsClusterSpecifierPluginRegistry&) = delete;

  // Movable.
  XdsClusterSpecifierPluginRegistry(
      XdsClusterSpecifierPluginRegistry&& other) noexcept
      : registry_(std::move(other.registry_)) {}
  XdsClusterSpecifierPluginRegistry& operator=(
      XdsClusterSpecifierPluginRegistry&& other) noexcept {
    registry_ = std::move(other.registry_);
    return *this;
  }

  void RegisterPlugin(std::unique_ptr<XdsClusterSpecifierPluginImpl> plugin,
                      absl::string_view config_proto_type_name);

  void PopulateSymtab(upb_DefPool* symtab) const;

  const XdsClusterSpecifierPluginImpl* GetPluginForType(
      absl::string_view config_proto_type_name) const;

 private:
  std::map<absl::string_view, std::unique_ptr<XdsClusterSpecifierPluginImpl>>
      registry_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_H
