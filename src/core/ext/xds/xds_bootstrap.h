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
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

bool XdsFederationEnabled();

class XdsClient;

class XdsCertificateProviderPluginMapInterface {
 public:
  virtual ~XdsCertificateProviderPluginMapInterface() = default;

  virtual absl::Status AddPlugin(const std::string& instance_name,
                                 const std::string& plugin_name,
                                 const Json& config) = 0;

  virtual bool HasPlugin(const std::string& instance_name) const = 0;

  virtual std::string ToString() const = 0;
};

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
    std::string server_uri;
    std::string channel_creds_type;
    Json channel_creds_config;
    std::set<std::string> server_features;

    static XdsServer Parse(const Json& json, grpc_error_handle* error);

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

    Json::Object ToJson() const;

    bool ShouldUseV3() const;
    bool IgnoreResourceDeletion() const;
  };

  struct Authority {
    std::string client_listener_resource_name_template;
    std::vector<XdsServer> xds_servers;
  };

  // Creates bootstrap object from json_string.
  // If *error is not GRPC_ERROR_NONE after returning, then there was an
  // error parsing the contents.
  static std::unique_ptr<XdsBootstrap> Create(
      absl::string_view json_string,
      std::unique_ptr<XdsCertificateProviderPluginMapInterface>
          certificate_provider_plugin_map,
      grpc_error_handle* error);

  // Do not instantiate directly -- use Create() above instead.
  XdsBootstrap(Json json,
               std::unique_ptr<XdsCertificateProviderPluginMapInterface>
                   certificate_provider_plugin_map,
               grpc_error_handle* error);

  std::string ToString() const;

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  const XdsServer& server() const { return servers_[0]; }
  const Node* node() const { return node_.get(); }
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
  const XdsCertificateProviderPluginMapInterface*
  certificate_provider_plugin_map() const {
    return certificate_provider_plugin_map_.get();
  }
  // A util method to check that an xds server exists in this bootstrap file.
  bool XdsServerExists(const XdsServer& server) const;

 private:
  grpc_error_handle ParseXdsServerList(Json* json,
                                       std::vector<XdsServer>* servers);
  grpc_error_handle ParseAuthorities(Json* json);
  grpc_error_handle ParseAuthority(Json* json, const std::string& name);
  grpc_error_handle ParseNode(Json* json);
  grpc_error_handle ParseLocality(Json* json);
  grpc_error_handle ParseCertificateProviders(Json* json);
  grpc_error_handle ParseCertificateProvider(const std::string& instance_name,
                                             Json* certificate_provider_json);

  std::vector<XdsServer> servers_;
  std::unique_ptr<Node> node_;
  std::string client_default_listener_resource_name_template_;
  std::string server_listener_resource_name_template_;
  std::map<std::string, Authority> authorities_;
  std::unique_ptr<XdsCertificateProviderPluginMapInterface>
      certificate_provider_plugin_map_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
