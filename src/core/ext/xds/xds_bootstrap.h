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

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_object_loader.h"

namespace grpc_core {

bool XdsFederationEnabled();

class XdsClient;

class XdsBootstrap {
 public:
  struct Node {
    std::string id;
    std::string cluster;

    struct Locality {
      std::string region;
      std::string zone;
      std::string sub_zone;

      static const JsonLoaderInterface* JsonLoader();
    };
    Locality locality;

    Json::Object metadata;

    static const JsonLoaderInterface* JsonLoader();
  };

  struct XdsServer {
    std::string server_uri;
    std::string channel_creds_type;
    Json::Object channel_creds_config;
    std::set<std::string> server_features;

    static const JsonLoaderInterface* JsonLoader();
    void JsonPostLoad(const Json& json, ErrorList* errors);

    bool operator==(const XdsServer& other) const {
      return (server_uri == other.server_uri &&
              channel_creds_type == other.channel_creds_type &&
              channel_creds_config == other.channel_creds_config &&
              server_features == other.server_features);
    }

    bool operator<(const XdsServer& other) const {
      if (server_uri < other.server_uri) return true;
      if (channel_creds_type < other.channel_creds_type) return true;
      if (Json{channel_creds_config}.Dump() <
          Json{other.channel_creds_config}.Dump()) {
        return true;
      }
      if (server_features < other.server_features) return true;
      return false;
    }

    Json::Object ToJson() const;

    bool ShouldUseV3() const;
    bool IgnoreResourceDeletion() const;
  };

  struct Authority {
    std::string client_listener_resource_name_template;
    std::vector<XdsServer> xds_servers;

    static const JsonLoaderInterface* JsonLoader();
  };

  // Creates bootstrap object from json_string.
  static absl::StatusOr<XdsBootstrap> Create(absl::string_view json_string);

  // Do not instantiate directly -- use Create() above instead.
  XdsBootstrap() = default;

  // Copyable.
  XdsBootstrap(const XdsBootstrap&);
  XdsBootstrap& operator=(const XdsBootstrap&);

  // Movable.
  XdsBootstrap(XdsBootstrap&&) noexcept;
  XdsBootstrap& operator=(XdsBootstrap&&) noexcept;

  static const JsonLoaderInterface* JsonLoader();
  void JsonPostLoad(const Json& json, ErrorList* errors);

  std::string ToString() const;

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  const XdsServer& server() const { return servers_[0]; }
  const Node* node() const { return node_.has_value() ? &*node_ : nullptr; }
  const std::string& client_default_listener_resource_name_template() const {
    return client_default_listener_resource_name_template_;
  }
  const std::string& server_listener_resource_name_template() const {
    return server_listener_resource_name_template_;
  }
  const std::map<std::string, Authority>& authorities() const {
    return authorities_;
  }
  const Authority* LookupAuthority(const std::string& name) const;
  const CertificateProviderStore::PluginDefinitionMap& certificate_providers()
      const {
    return certificate_providers_;
  }
  // A util method to check that an xds server exists in this bootstrap file.
  bool XdsServerExists(const XdsServer& server) const;

 private:
  std::vector<XdsServer> servers_;
  absl::optional<Node> node_;
  std::string client_default_listener_resource_name_template_;
  std::string server_listener_resource_name_template_;
  std::map<std::string, Authority> authorities_;
  CertificateProviderStore::PluginDefinitionMap certificate_providers_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
