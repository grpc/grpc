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

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/optional.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_hash_table.h"

namespace grpc_core {
namespace internal {

class ClientChannelGlobalParsedObject : public ServiceConfig::ParsedConfig {
 public:
  struct RetryThrottling {
    intptr_t max_milli_tokens = 0;
    intptr_t milli_token_ratio = 0;
  };

  ClientChannelGlobalParsedObject(
      RefCountedPtr<ParsedLoadBalancingConfig> parsed_lb_config,
      UniquePtr<char> parsed_deprecated_lb_policy,
      const Optional<RetryThrottling>& retry_throttling,
      const char* health_check_service_name)
      : parsed_lb_config_(std::move(parsed_lb_config)),
        parsed_deprecated_lb_policy_(std::move(parsed_deprecated_lb_policy)),
        retry_throttling_(retry_throttling),
        health_check_service_name_(health_check_service_name) {}

  Optional<RetryThrottling> retry_throttling() const {
    return retry_throttling_;
  }

  RefCountedPtr<ParsedLoadBalancingConfig> parsed_lb_config() const {
    return parsed_lb_config_;
  }

  const char* parsed_deprecated_lb_policy() const {
    return parsed_deprecated_lb_policy_.get();
  }

  const char* health_check_service_name() const {
    return health_check_service_name_;
  }

 private:
  RefCountedPtr<ParsedLoadBalancingConfig> parsed_lb_config_;
  UniquePtr<char> parsed_deprecated_lb_policy_;
  Optional<RetryThrottling> retry_throttling_;
  const char* health_check_service_name_;
};

class ClientChannelMethodParsedObject : public ServiceConfig::ParsedConfig {
 public:
  struct RetryPolicy {
    int max_attempts = 0;
    grpc_millis initial_backoff = 0;
    grpc_millis max_backoff = 0;
    float backoff_multiplier = 0;
    StatusCodeSet retryable_status_codes;
  };

  ClientChannelMethodParsedObject(grpc_millis timeout,
                                  const Optional<bool>& wait_for_ready,
                                  UniquePtr<RetryPolicy> retry_policy)
      : timeout_(timeout),
        wait_for_ready_(wait_for_ready),
        retry_policy_(std::move(retry_policy)) {}

  grpc_millis timeout() const { return timeout_; }

  Optional<bool> wait_for_ready() const { return wait_for_ready_; }

  const RetryPolicy* retry_policy() const { return retry_policy_.get(); }

 private:
  grpc_millis timeout_ = 0;
  Optional<bool> wait_for_ready_;
  UniquePtr<RetryPolicy> retry_policy_;
};

class ClientChannelServiceConfigParser : public ServiceConfig::Parser {
 public:
  UniquePtr<ServiceConfig::ParsedConfig> ParseGlobalParams(
      const grpc_json* json, grpc_error** error) override;

  UniquePtr<ServiceConfig::ParsedConfig> ParsePerMethodParams(
      const grpc_json* json, grpc_error** error) override;

  static size_t ParserIndex();
  static void Register();
};

// TODO(yashykt): It would be cleaner to move this logic to the client_channel
// code. A container of processed fields from the resolver result. Simplifies
// the usage of resolver result.
class ProcessedResolverResult {
 public:
  // Processes the resolver result and populates the relative members
  // for later consumption.
  ProcessedResolverResult(const Resolver::Result& resolver_result);

  ~ProcessedResolverResult() { GRPC_ERROR_UNREF(service_config_error_); }

  // Getters. Any managed object's ownership is transferred.
  const char* service_config_json() { return service_config_json_; }

  RefCountedPtr<ServiceConfig> service_config() { return service_config_; }

  UniquePtr<char> lb_policy_name() { return std::move(lb_policy_name_); }
  RefCountedPtr<ParsedLoadBalancingConfig> lb_policy_config() {
    return lb_policy_config_;
  }

  Optional<ClientChannelGlobalParsedObject::RetryThrottling>
  retry_throttle_data() {
    return retry_throttle_data_;
  }

  const char* health_check_service_name() { return health_check_service_name_; }

  grpc_error* service_config_error() {
    grpc_error* return_error = service_config_error_;
    service_config_error_ = GRPC_ERROR_NONE;
    return return_error;
  }

 private:
  // Finds the service config; extracts LB config and (maybe) retry throttle
  // params from it.
  void ProcessServiceConfig(
      const Resolver::Result& resolver_result,
      const ClientChannelGlobalParsedObject* parsed_object);

  // Extracts the LB policy.
  void ProcessLbPolicy(const Resolver::Result& resolver_result,
                       const ClientChannelGlobalParsedObject* parsed_object);

  // Parses the service config. Intended to be used by
  // ServiceConfig::ParseGlobalParams.
  static void ParseServiceConfig(const grpc_json* field,
                                 ProcessedResolverResult* parsing_state);
  // Parses the LB config from service config.
  void ParseLbConfigFromServiceConfig(const grpc_json* field);
  // Parses the retry throttle parameters from service config.
  void ParseRetryThrottleParamsFromServiceConfig(const grpc_json* field);

  // Service config.
  const char* service_config_json_ = nullptr;
  RefCountedPtr<ServiceConfig> service_config_;
  grpc_error* service_config_error_ = GRPC_ERROR_NONE;
  // LB policy.
  UniquePtr<char> lb_policy_name_;
  RefCountedPtr<ParsedLoadBalancingConfig> lb_policy_config_;
  // Retry throttle data.
  Optional<ClientChannelGlobalParsedObject::RetryThrottling>
      retry_throttle_data_;
  const char* health_check_service_name_ = nullptr;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H */
