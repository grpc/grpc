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

#ifndef GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
#define GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H

#include <grpc/support/port_platform.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/core/lib/json/json.h"

namespace grpc_core {

bool XdsFederationEnabled();

class XdsBootstrap {
 public:
  struct Node {
    std::string id;
    std::string cluster;
    std::string locality_region;
    std::string locality_zone;
    std::string locality_sub_zone;
    Json metadata;
  };

  struct XdsServer {
    static constexpr absl::string_view kServerFeatureXdsV3 = "xds_v3";
    static constexpr absl::string_view kServerFeatureIgnoreResourceDeletion =
        "ignore_resource_deletion";

    std::string server_uri;
    std::string channel_creds_type;
    Json channel_creds_config;
    std::set<std::string> server_features;

    bool operator==(const XdsServer& other) const {
      return (server_uri == other.server_uri &&
              channel_creds_type == other.channel_creds_type &&
              channel_creds_config == other.channel_creds_config &&
              server_features == other.server_features);
    }

    bool operator<(const XdsServer& other) const {
      if (server_uri < other.server_uri) return true;
      if (channel_creds_type < other.channel_creds_type) return true;
      if (channel_creds_config.Dump() < other.channel_creds_config.Dump()) {
        return true;
      }
      if (server_features < other.server_features) return true;
      return false;
    }

    bool ShouldUseV3() const;
    bool IgnoreResourceDeletion() const;
  };

  struct Authority {
    std::string client_listener_resource_name_template;
    std::vector<XdsServer> xds_servers;
  };

  virtual ~XdsBootstrap() = default;

  virtual std::string ToString() const = 0;

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  virtual const XdsServer& server() const = 0;
  virtual const Node* node() const = 0;
  virtual const std::string& client_default_listener_resource_name_template()
      const = 0;
  virtual const std::string& server_listener_resource_name_template() const = 0;
  virtual const std::map<std::string, Authority>& authorities() const = 0;

  // Returns a pointer to the specified authority, or null if it does
  // not exist in this bootstrap config.
  const Authority* LookupAuthority(const std::string& name) const;

  // A utility method to check that an xDS server exists in this
  // bootstrap config.
  bool XdsServerExists(const XdsServer& server) const;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
