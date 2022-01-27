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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_SERVICE_CONFIG_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_SERVICE_CONFIG_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/service_config/service_config_parser.h"

namespace grpc_core {
namespace internal {

class RetryGlobalConfig : public ServiceConfigParser::ParsedConfig {
 public:
  RetryGlobalConfig(intptr_t max_milli_tokens, intptr_t milli_token_ratio)
      : max_milli_tokens_(max_milli_tokens),
        milli_token_ratio_(milli_token_ratio) {}

  intptr_t max_milli_tokens() const { return max_milli_tokens_; }
  intptr_t milli_token_ratio() const { return milli_token_ratio_; }

 private:
  intptr_t max_milli_tokens_ = 0;
  intptr_t milli_token_ratio_ = 0;
};

class RetryMethodConfig : public ServiceConfigParser::ParsedConfig {
 public:
  RetryMethodConfig(int max_attempts, grpc_millis initial_backoff,
                    grpc_millis max_backoff, float backoff_multiplier,
                    StatusCodeSet retryable_status_codes,
                    absl::optional<grpc_millis> per_attempt_recv_timeout)
      : max_attempts_(max_attempts),
        initial_backoff_(initial_backoff),
        max_backoff_(max_backoff),
        backoff_multiplier_(backoff_multiplier),
        retryable_status_codes_(retryable_status_codes),
        per_attempt_recv_timeout_(per_attempt_recv_timeout) {}

  int max_attempts() const { return max_attempts_; }
  grpc_millis initial_backoff() const { return initial_backoff_; }
  grpc_millis max_backoff() const { return max_backoff_; }
  float backoff_multiplier() const { return backoff_multiplier_; }
  StatusCodeSet retryable_status_codes() const {
    return retryable_status_codes_;
  }
  absl::optional<grpc_millis> per_attempt_recv_timeout() const {
    return per_attempt_recv_timeout_;
  }

 private:
  int max_attempts_ = 0;
  grpc_millis initial_backoff_ = 0;
  grpc_millis max_backoff_ = 0;
  float backoff_multiplier_ = 0;
  StatusCodeSet retryable_status_codes_;
  absl::optional<grpc_millis> per_attempt_recv_timeout_;
};

class RetryServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const grpc_channel_args* /*args*/, const Json& json,
      grpc_error_handle* error) override;

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* args, const Json& json,
      grpc_error_handle* error) override;

  static size_t ParserIndex();
  static void Register();
};

}  // namespace internal
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_SERVICE_CONFIG_H
