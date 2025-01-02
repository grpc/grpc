//
// Copyright 2019 gRPC authors.
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

#include "src/core/resolver/xds/xds_config.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/core/util/match.h"

namespace grpc_core {

//
// XdsConfig::ClusterConfig
//

XdsConfig::ClusterConfig::ClusterConfig(
    std::shared_ptr<const XdsClusterResource> cluster,
    std::shared_ptr<const XdsEndpointResource> endpoints,
    std::string resolution_note)
    : cluster(std::move(cluster)),
      children(absl::in_place_type_t<EndpointConfig>(), std::move(endpoints),
               std::move(resolution_note)) {}

XdsConfig::ClusterConfig::ClusterConfig(
    std::shared_ptr<const XdsClusterResource> cluster,
    std::vector<absl::string_view> leaf_clusters)
    : cluster(std::move(cluster)),
      children(absl::in_place_type_t<AggregateConfig>(),
               std::move(leaf_clusters)) {}

//
// XdsConfig
//

std::string XdsConfig::ToString() const {
  std::vector<std::string> parts = {
      "{\n  listener: {",     listener->ToString(),
      "}\n  route_config: {", route_config->ToString(),
      "}\n  virtual_host: {", virtual_host->ToString(),
      "}\n  clusters: {\n"};
  for (const auto& p : clusters) {
    parts.push_back(absl::StrCat("    \"", p.first, "\": "));
    if (!p.second.ok()) {
      parts.push_back(p.second.status().ToString());
      parts.push_back("\n");
    } else {
      parts.push_back(
          absl::StrCat("      {\n"
                       "        cluster: {",
                       p.second->cluster->ToString(), "}\n"));
      Match(
          p.second->children,
          [&](const ClusterConfig::EndpointConfig& endpoint_config) {
            parts.push_back(
                absl::StrCat("        endpoints: {",
                             endpoint_config.endpoints == nullptr
                                 ? "<null>"
                                 : endpoint_config.endpoints->ToString(),
                             "}\n"
                             "        resolution_note: \"",
                             endpoint_config.resolution_note, "\"\n"));
          },
          [&](const ClusterConfig::AggregateConfig& aggregate_config) {
            parts.push_back(absl::StrCat(
                "        leaf_clusters: [",
                absl::StrJoin(aggregate_config.leaf_clusters, ", "), "]\n"));
          });
      parts.push_back(
          "      }\n"
          "    ]\n");
    }
  }
  parts.push_back("  }\n}");
  return absl::StrJoin(parts, "");
}

}  // namespace grpc_core
