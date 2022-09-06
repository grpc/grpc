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

#ifndef GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_GRPC_H
#define GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_GRPC_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"

namespace grpc_core {

class GrpcXdsBootstrap : public XdsBootstrap {
 public:
  // Creates bootstrap object from json_string.
  static absl::StatusOr<std::unique_ptr<GrpcXdsBootstrap>> Create(
      absl::string_view json_string);

  // Do not instantiate directly -- use Create() above instead.
  GrpcXdsBootstrap(
      std::vector<XdsServer> servers, absl::optional<Node> node,
      std::string client_default_listener_resource_name_template,
      std::string server_listener_resource_name_template,
      std::map<std::string, Authority> authorities,
      CertificateProviderStore::PluginDefinitionMap certificate_providers);

  std::string ToString() const override;

  const XdsServer& server() const override { return servers_[0]; }
  const Node* node() const override {
    return node_.has_value() ? &*node_ : nullptr;
  }
  const std::string& client_default_listener_resource_name_template()
      const override {
    return client_default_listener_resource_name_template_;
  }
  const std::string& server_listener_resource_name_template() const override {
    return server_listener_resource_name_template_;
  }
  const std::map<std::string, Authority>& authorities() const override {
    return authorities_;
  }

  const CertificateProviderStore::PluginDefinitionMap& certificate_providers()
      const {
    return certificate_providers_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs& args, ErrorList* errors);

  static absl::StatusOr<XdsServer> XdsServerParse(const Json& json);
  static Json::Object XdsServerToJson(const XdsServer& server);

 private:
  std::vector<XdsServer> servers_;
  absl::optional<Node> node_;
  std::string client_default_listener_resource_name_template_;
  std::string server_listener_resource_name_template_;
  std::map<std::string, Authority> authorities_;
  CertificateProviderStore::PluginDefinitionMap certificate_providers_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_GRPC_H
