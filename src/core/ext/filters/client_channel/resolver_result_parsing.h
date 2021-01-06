/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H

#include <grpc/support/port_platform.h>

#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/json/json.h"

// Channel arg key for enabling parsing fault injection via method config.
#define GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG \
  "grpc.parse_fault_injection_method_config"

namespace grpc_core {
namespace internal {

class ClientChannelGlobalParsedConfig
    : public ServiceConfigParser::ParsedConfig {
 public:
  struct RetryThrottling {
    intptr_t max_milli_tokens = 0;
    intptr_t milli_token_ratio = 0;
  };

  ClientChannelGlobalParsedConfig(
      RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config,
      std::string parsed_deprecated_lb_policy,
      const absl::optional<RetryThrottling>& retry_throttling,
      absl::optional<std::string> health_check_service_name)
      : parsed_lb_config_(std::move(parsed_lb_config)),
        parsed_deprecated_lb_policy_(std::move(parsed_deprecated_lb_policy)),
        retry_throttling_(retry_throttling),
        health_check_service_name_(std::move(health_check_service_name)) {}

  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config() const {
    return parsed_lb_config_;
  }

  const std::string& parsed_deprecated_lb_policy() const {
    return parsed_deprecated_lb_policy_;
  }

  absl::optional<RetryThrottling> retry_throttling() const {
    return retry_throttling_;
  }

  const absl::optional<std::string>& health_check_service_name() const {
    return health_check_service_name_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> parsed_lb_config_;
  std::string parsed_deprecated_lb_policy_;
  absl::optional<RetryThrottling> retry_throttling_;
  absl::optional<std::string> health_check_service_name_;
};

class ClientChannelMethodParsedConfig
    : public ServiceConfigParser::ParsedConfig {
 public:
  struct RetryPolicy {
    int max_attempts = 0;
    grpc_millis initial_backoff = 0;
    grpc_millis max_backoff = 0;
    float backoff_multiplier = 0;
    StatusCodeSet retryable_status_codes;
  };

  struct FaultInjectionPolicy {
    uint32_t abort_per_million = 0;
    grpc_status_code abort_code = GRPC_STATUS_OK;
    std::string abort_message;
    std::string abort_code_header;
    std::string abort_per_million_header;

    uint32_t delay_per_million = 0;
    grpc_millis delay = 0;
    std::string delay_header;
    std::string delay_per_million_header;

    // By default, the max allowed active faults are unlimited.
    uint32_t max_faults = std::numeric_limits<uint32_t>::max();
  };

  ClientChannelMethodParsedConfig(
      grpc_millis timeout, const absl::optional<bool>& wait_for_ready,
      std::unique_ptr<RetryPolicy> retry_policy,
      std::unique_ptr<FaultInjectionPolicy> fault_injection_policy)
      : timeout_(timeout),
        wait_for_ready_(wait_for_ready),
        retry_policy_(std::move(retry_policy)),
        fault_injection_policy_(std::move(fault_injection_policy)) {}

  grpc_millis timeout() const { return timeout_; }

  absl::optional<bool> wait_for_ready() const { return wait_for_ready_; }

  const RetryPolicy* retry_policy() const { return retry_policy_.get(); }
  const FaultInjectionPolicy* fault_injection_policy() const {
    return fault_injection_policy_.get();
  }

 private:
  grpc_millis timeout_ = 0;
  absl::optional<bool> wait_for_ready_;
  std::unique_ptr<RetryPolicy> retry_policy_;
  std::unique_ptr<FaultInjectionPolicy> fault_injection_policy_;
};

class ClientChannelServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const grpc_channel_args* /*args*/, const Json& json,
      grpc_error** error) override;

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* /*args*/, const Json& json,
      grpc_error** error) override;

  static size_t ParserIndex();
  static void Register();
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H */
