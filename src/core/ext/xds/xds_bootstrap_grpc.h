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
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

class XdsCertificateProviderPluginMapInterface {
 public:
  virtual ~XdsCertificateProviderPluginMapInterface() = default;

  virtual absl::Status AddPlugin(const std::string& instance_name,
                                 const std::string& plugin_name,
                                 const Json& config) = 0;

  virtual bool HasPlugin(const std::string& instance_name) const = 0;

  virtual std::string ToString() const = 0;
};

class GrpcXdsBootstrap : public XdsBootstrap {
 public:
  // Creates bootstrap object from json_string.
  // If *error is not GRPC_ERROR_NONE after returning, then there was an
  // error parsing the contents.
  static std::unique_ptr<GrpcXdsBootstrap> Create(
      absl::string_view json_string,
      std::unique_ptr<XdsCertificateProviderPluginMapInterface>
          certificate_provider_plugin_map,
      grpc_error_handle* error);

  // Do not instantiate directly -- use Create() above instead.
  GrpcXdsBootstrap(Json json,
                   std::unique_ptr<XdsCertificateProviderPluginMapInterface>
                       certificate_provider_plugin_map,
                   grpc_error_handle* error);

  std::string ToString() const override;

  const XdsServer& server() const override { return servers_[0]; }
  const Node* node() const override { return node_.get(); }
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

  const XdsCertificateProviderPluginMapInterface*
  certificate_provider_plugin_map() const {
    return certificate_provider_plugin_map_.get();
  }

  static XdsServer XdsServerParse(const Json& json, grpc_error_handle* error);
  static Json::Object XdsServerToJson(const XdsServer& server);

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

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_GRPC_H
