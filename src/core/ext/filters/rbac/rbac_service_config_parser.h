//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_RBAC_RBAC_SERVICE_CONFIG_PARSER_H
#define GRPC_CORE_EXT_FILTERS_RBAC_RBAC_SERVICE_CONFIG_PARSER_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/service_config/service_config_parser.h"

namespace grpc_core {

// Channel arg key for enabling parsing RBAC via method config.
#define GRPC_ARG_PARSE_RBAC_METHOD_CONFIG \
  "grpc.internal.parse_rbac_method_config"

class RbacMethodParsedConfig : public ServiceConfigParser::ParsedConfig {
 public:
  explicit RbacMethodParsedConfig(std::vector<Rbac> rbac_policies) {
    for (auto& rbac_policy : rbac_policies) {
      authorization_engines_.emplace_back(std::move(rbac_policy));
    }
  }

  // Returns the authorization engine for a rbac policy at a certain index. For
  // a connection on the server, multiple RBAC policies might be active. The
  // RBAC filter uses this method to get the RBAC policy configured for a
  // instance at a particular instance.
  const GrpcAuthorizationEngine* authorization_engine(int index) const {
    if (static_cast<size_t>(index) >= authorization_engines_.size()) {
      return nullptr;
    }
    return &authorization_engines_[index];
  }

 private:
  std::vector<GrpcAuthorizationEngine> authorization_engines_;
};

class RbacServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  // Parses the per-method service config for rbac filter.
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* args, const Json& json,
      grpc_error_handle* error) override;
  // Returns the parser index for RbacServiceConfigParser.
  static size_t ParserIndex();
  // Registers RbacServiceConfigParser to ServiceConfigParser.
  static void Register();
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_RBAC_RBAC_SERVICE_CONFIG_PARSER_H
