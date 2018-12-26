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

#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/transport/service_config.h"

namespace grpc_core {
namespace internal {

class ClientChannelMethodParams;

// A table mapping from a method name to its method parameters.
typedef SliceHashTable<RefCountedPtr<ClientChannelMethodParams>>
    ClientChannelMethodParamsTable;

// A container of processed fields from the resolver result. Simplifies the
// usage of resolver result.
class ProcessedResolverResult {
 public:
  // Processes the resolver result and populates the relative members
  // for later consumption. Tries to parse retry parameters only if parse_retry
  // is true.
  ProcessedResolverResult(const grpc_channel_args& resolver_result,
                          bool parse_retry);

  // Getters. Any managed object's ownership is transferred.
  UniquePtr<char> service_config_json() {
    return std::move(service_config_json_);
  }
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data() {
    return std::move(retry_throttle_data_);
  }
  RefCountedPtr<ClientChannelMethodParamsTable> method_params_table() {
    return std::move(method_params_table_);
  }
  UniquePtr<char> lb_policy_name() { return std::move(lb_policy_name_); }
  grpc_json* lb_policy_config() { return lb_policy_config_; }

 private:
  // Finds the service config; extracts LB config and (maybe) retry throttle
  // params from it.
  void ProcessServiceConfig(const grpc_channel_args& resolver_result,
                            bool parse_retry);

  // Finds the LB policy name (when no LB config was found).
  void ProcessLbPolicyName(const grpc_channel_args& resolver_result);

  // Parses the service config. Intended to be used by
  // ServiceConfig::ParseGlobalParams.
  static void ParseServiceConfig(const grpc_json* field,
                                 ProcessedResolverResult* parsing_state);
  // Parses the LB config from service config.
  void ParseLbConfigFromServiceConfig(const grpc_json* field);
  // Parses the retry throttle parameters from service config.
  void ParseRetryThrottleParamsFromServiceConfig(const grpc_json* field);

  // Service config.
  UniquePtr<char> service_config_json_;
  UniquePtr<grpc_core::ServiceConfig> service_config_;
  // LB policy.
  grpc_json* lb_policy_config_ = nullptr;
  UniquePtr<char> lb_policy_name_;
  // Retry throttle data.
  char* server_name_ = nullptr;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  // Method params table.
  RefCountedPtr<ClientChannelMethodParamsTable> method_params_table_;
};

// The parameters of a method.
class ClientChannelMethodParams : public RefCounted<ClientChannelMethodParams> {
 public:
  enum WaitForReady {
    WAIT_FOR_READY_UNSET = 0,
    WAIT_FOR_READY_FALSE,
    WAIT_FOR_READY_TRUE
  };

  struct RetryPolicy {
    int max_attempts = 0;
    grpc_millis initial_backoff = 0;
    grpc_millis max_backoff = 0;
    float backoff_multiplier = 0;
    StatusCodeSet retryable_status_codes;
  };

  /// Creates a method_parameters object from \a json.
  /// Intended for use with ServiceConfig::CreateMethodConfigTable().
  static RefCountedPtr<ClientChannelMethodParams> CreateFromJson(
      const grpc_json* json);

  grpc_millis timeout() const { return timeout_; }
  WaitForReady wait_for_ready() const { return wait_for_ready_; }
  const RetryPolicy* retry_policy() const { return retry_policy_.get(); }

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* grpc_core::New(Args&&... args);

  // So Delete() can call our private dtor.
  template <typename T>
  friend void grpc_core::Delete(T*);

  ClientChannelMethodParams() {}
  virtual ~ClientChannelMethodParams() {}

  grpc_millis timeout_ = 0;
  WaitForReady wait_for_ready_ = WAIT_FOR_READY_UNSET;
  UniquePtr<RetryPolicy> retry_policy_;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_RESULT_PARSING_H */
