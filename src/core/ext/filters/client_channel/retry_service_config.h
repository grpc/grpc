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

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
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
  RetryMethodConfig(int max_attempts, Duration initial_backoff,
                    Duration max_backoff, float backoff_multiplier,
                    StatusCodeSet retryable_status_codes,
                    absl::optional<Duration> per_attempt_recv_timeout)
      : max_attempts_(max_attempts),
        initial_backoff_(initial_backoff),
        max_backoff_(max_backoff),
        backoff_multiplier_(backoff_multiplier),
        retryable_status_codes_(retryable_status_codes),
        per_attempt_recv_timeout_(per_attempt_recv_timeout) {}

  int max_attempts() const { return max_attempts_; }
  Duration initial_backoff() const { return initial_backoff_; }
  Duration max_backoff() const { return max_backoff_; }
  float backoff_multiplier() const { return backoff_multiplier_; }
  StatusCodeSet retryable_status_codes() const {
    return retryable_status_codes_;
  }
  absl::optional<Duration> per_attempt_recv_timeout() const {
    return per_attempt_recv_timeout_;
  }

 private:
  int max_attempts_ = 0;
  Duration initial_backoff_;
  Duration max_backoff_;
  float backoff_multiplier_ = 0;
  StatusCodeSet retryable_status_codes_;
  absl::optional<Duration> per_attempt_recv_timeout_;
};

class RetryServiceConfigParser : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return parser_name(); }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParseGlobalParams(const ChannelArgs& /*args*/, const Json& json) override;

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParsePerMethodParams(const ChannelArgs& args, const Json& json) override;

  static size_t ParserIndex();
  static void Register(CoreConfiguration::Builder* builder);

 private:
  static absl::string_view parser_name() { return "retry"; }
};

}  // namespace internal
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_SERVICE_CONFIG_H
