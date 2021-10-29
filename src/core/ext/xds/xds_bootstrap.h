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

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"

#include <grpc/slice.h>

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

class XdsClient;

class XdsChannelCredsRegistry {
 public:
  static bool IsSupported(const std::string& creds_type);
  static bool IsValidConfig(const std::string& creds_type, const Json& config);
  static RefCountedPtr<grpc_channel_credentials> MakeChannelCreds(
      const std::string& creds_type, const Json& config);
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

    bool ShouldUseV3() const;
  };

  // Creates bootstrap object from json_string.
  // If *error is not GRPC_ERROR_NONE after returning, then there was an
  // error parsing the contents.
  static std::unique_ptr<XdsBootstrap> Create(absl::string_view json_string,
                                              grpc_error_handle* error);

  // Do not instantiate directly -- use Create() above instead.
  XdsBootstrap(Json json, grpc_error_handle* error);

  std::string ToString() const;

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  const XdsServer& server() const { return servers_[0]; }
  const Node* node() const { return node_.get(); }
  const std::string& server_listener_resource_name_template() const {
    return server_listener_resource_name_template_;
  }

  const CertificateProviderStore::PluginDefinitionMap& certificate_providers()
      const {
    return certificate_providers_;
  }

 private:
  grpc_error_handle ParseXdsServerList(Json* json);
  grpc_error_handle ParseXdsServer(Json* json, size_t idx);
  grpc_error_handle ParseChannelCredsArray(Json* json, XdsServer* server);
  grpc_error_handle ParseChannelCreds(Json* json, size_t idx,
                                      XdsServer* server);
  grpc_error_handle ParseServerFeaturesArray(Json* json, XdsServer* server);
  grpc_error_handle ParseNode(Json* json);
  grpc_error_handle ParseLocality(Json* json);
  grpc_error_handle ParseCertificateProviders(Json* json);
  grpc_error_handle ParseCertificateProvider(const std::string& instance_name,
                                             Json* certificate_provider_json);

  absl::InlinedVector<XdsServer, 1> servers_;
  std::unique_ptr<Node> node_;
  std::string server_listener_resource_name_template_;
  CertificateProviderStore::PluginDefinitionMap certificate_providers_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H */
