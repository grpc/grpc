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

#ifndef GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_GRPC_H
#define GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_GRPC_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"
#include "upb/upb.h"

#include "src/core/ext/xds/xds_cluster_specifier_plugin.h"

namespace grpc_core {

class XdsRouteLookupClusterSpecifierPlugin
    : public XdsClusterSpecifierPlugin {
  absl::string_view ConfigProtoType() const override;

  void PopulateSymtab(upb_DefPool* symtab) const override;

  absl::StatusOr<std::string> GenerateLoadBalancingPolicyConfig(
      upb_StringView serialized_plugin_config, upb_Arena* arena,
      upb_DefPool* symtab) const override;
};

void RegisterGrpcXdsClusterSpecifierPlugins(
    XdsClusterSpecifierPluginRegistry* registry);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CLUSTER_SPECIFIER_PLUGIN_GRPC_H
