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

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
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
  ClientChannelMethodParsedConfig(Duration timeout,
                                  const absl::optional<bool>& wait_for_ready)
      : timeout_(timeout), wait_for_ready_(wait_for_ready) {}

  Duration timeout() const { return timeout_; }

  absl::optional<bool> wait_for_ready() const { return wait_for_ready_; }

 private:
  Duration timeout_;
  absl::optional<bool> wait_for_ready_;
};

class ClientChannelServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return parser_name(); }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParseGlobalParams(const ChannelArgs& /*args*/, const Json& json) override;

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParsePerMethodParams(const ChannelArgs& /*args*/, const Json& json) override;

  static size_t ParserIndex();
  static void Register(CoreConfiguration::Builder* builder);

 private:
  static absl::string_view parser_name() { return "client_channel"; }
};

}  // namespace internal
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H
