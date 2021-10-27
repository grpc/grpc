//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H

#include <grpc/support/port_platform.h>

#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/json/json.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/service_config/service_config_parser.h"

namespace grpc_core {
namespace internal {

class ClientChannelGlobalParsedConfig
    : public ServiceConfigParser::ParsedConfig {
 public:
  ClientChannelGlobalParsedConfig(
      RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config,
      std::string parsed_deprecated_lb_policy,
      absl::optional<std::string> health_check_service_name)
      : parsed_lb_config_(std::move(parsed_lb_config)),
        parsed_deprecated_lb_policy_(std::move(parsed_deprecated_lb_policy)),
        health_check_service_name_(std::move(health_check_service_name)) {}

  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config() const {
    return parsed_lb_config_;
  }

  const std::string& parsed_deprecated_lb_policy() const {
    return parsed_deprecated_lb_policy_;
  }

  const absl::optional<std::string>& health_check_service_name() const {
    return health_check_service_name_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config_;
  std::string parsed_deprecated_lb_policy_;
  absl::optional<std::string> health_check_service_name_;
};

class ClientChannelMethodParsedConfig
    : public ServiceConfigParser::ParsedConfig {
 public:
  ClientChannelMethodParsedConfig(grpc_millis timeout,
                                  const absl::optional<bool>& wait_for_ready)
      : timeout_(timeout), wait_for_ready_(wait_for_ready) {}

  grpc_millis timeout() const { return timeout_; }

  absl::optional<bool> wait_for_ready() const { return wait_for_ready_; }

 private:
  grpc_millis timeout_ = 0;
  absl::optional<bool> wait_for_ready_;
};

class ClientChannelServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const grpc_channel_args* /*args*/, const Json& json,
      grpc_error_handle* error) override;

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* /*args*/, const Json& json,
      grpc_error_handle* error) override;

  static size_t ParserIndex();
  static void Register();
};

}  // namespace internal
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H
