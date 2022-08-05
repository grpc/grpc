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

namespace grpc_core {

void XdsClusterSpecifierPluginRegistry::RegisterPlugin(
    std::unique_ptr<XdsClusterSpecifierPlugin> plugin) {
  plugins_[plugin->ConfigProtoType()] = std::move(plugin);
}

void XdsClusterSpecifierPluginRegistry::PopulateSymtab(
    upb_DefPool* symtab) const {
  for (const auto& p : plugins_) {
    p.second->PopulateSymtab(symtab);
  }
}

const XdsClusterSpecifierPlugin*
XdsClusterSpecifierPluginRegistry::GetPluginForType(
    absl::string_view config_proto_type_name) const {
  auto it = plugins_.find(config_proto_type_name);
  if (it == plugins_.end()) return nullptr;
  return it->second.get();
}

}  // namespace grpc_core
