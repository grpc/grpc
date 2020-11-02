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
#include "src/core/lib/gprpp/map.h"
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
    std::string locality_subzone;
    Json metadata;
  };

  struct XdsServer {
    std::string server_uri;
    std::string channel_creds_type;
    Json channel_creds_config;
    std::set<std::string> server_features;

    bool ShouldUseV3() const;
  };

  // If *error is not GRPC_ERROR_NONE after returning, then there was an
  // error reading the file.
  static std::unique_ptr<XdsBootstrap> ReadFromFile(XdsClient* client,
                                                    TraceFlag* tracer,
                                                    grpc_error** error);

  // Do not instantiate directly -- use ReadFromFile() above instead.
  XdsBootstrap(Json json, grpc_error** error);

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  const XdsServer& server() const { return servers_[0]; }
  const Node* node() const { return node_.get(); }

  const CertificateProviderStore::PluginDefinitionMap& certificate_providers()
      const {
    return certificate_providers_;
  }

 private:
  grpc_error* ParseXdsServerList(Json* json);
  grpc_error* ParseXdsServer(Json* json, size_t idx);
  grpc_error* ParseChannelCredsArray(Json* json, XdsServer* server);
  grpc_error* ParseChannelCreds(Json* json, size_t idx, XdsServer* server);
  grpc_error* ParseServerFeaturesArray(Json* json, XdsServer* server);
  grpc_error* ParseNode(Json* json);
  grpc_error* ParseLocality(Json* json);
  grpc_error* ParseCertificateProviders(Json* json);
  grpc_error* ParseCertificateProvider(const std::string& instance_name,
                                       Json* certificate_provider_json);

  absl::InlinedVector<XdsServer, 1> servers_;
  std::unique_ptr<Node> node_;
  CertificateProviderStore::PluginDefinitionMap certificate_providers_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H */
