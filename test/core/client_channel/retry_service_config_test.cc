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

#include "src/core/client_channel/retry_service_config.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/service_config/service_config_parser.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {

class RetryParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    parser_index_ =
        CoreConfiguration::Get().service_config_parser().GetParserIndex(
            "retry");
  }

  size_t parser_index_;
};

TEST_F(RetryParserTest, ValidRetryThrottling) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config = static_cast<internal::RetryGlobalConfig*>(
      (*service_config)->GetGlobalParsedConfig(parser_index_));
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_milli_tokens(), 2000);
  EXPECT_EQ(parsed_config->milli_token_ratio(), 1000);
}

TEST_F(RetryParserTest, RetryThrottlingMissingFields) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:retryThrottling.maxTokens error:field not present; "
            "field:retryThrottling.tokenRatio error:field not present]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryThrottlingNegativeMaxTokens) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": -2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:retryThrottling.maxTokens error:"
            "failed to parse non-negative number]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryThrottlingInvalidTokenRatio) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": -1\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:retryThrottling.tokenRatio error:"
            "could not parse as a number]");
}

TEST_F(RetryParserTest, ValidRetryPolicy) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 3,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config = static_cast<internal::RetryMethodConfig*>(
      ((*vector_ptr)[parser_index_]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 3);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), std::nullopt);
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest, InvalidRetryPolicyWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": 5\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy error:is not an object]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyRequiredFieldsMissing) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.backoffMultiplier "
            "error:field not present; "
            "field:methodConfig[0].retryPolicy.initialBackoff "
            "error:field not present; "
            "field:methodConfig[0].retryPolicy.maxAttempts "
            "error:field not present; "
            "field:methodConfig[0].retryPolicy.maxBackoff "
            "error:field not present]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxAttemptsWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": \"FOO\",\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.maxAttempts "
            "error:failed to parse number]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxAttemptsBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 1,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.maxAttempts "
            "error:must be at least 2]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyInitialBackoffWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1sec\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.initialBackoff "
            "error:Not a duration (no s suffix)]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyInitialBackoffBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"0s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.initialBackoff "
            "error:must be greater than 0]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxBackoffWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120sec\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.maxBackoff "
            "error:Not a duration (no s suffix)]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxBackoffBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"0s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.maxBackoff "
            "error:must be greater than 0]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyBackoffMultiplierWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": [],\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.backoffMultiplier "
            "error:is not a number]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyBackoffMultiplierBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 0,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.backoffMultiplier "
            "error:must be greater than 0]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyEmptyRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": []\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.retryableStatusCodes "
            "error:must be non-empty]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyRetryableStatusCodesWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": 0\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.retryableStatusCodes "
            "error:is not an array]")
      << service_config.status();
}

TEST_F(RetryParserTest,
       InvalidRetryPolicyRetryableStatusCodesElementsWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": [true, 2]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.retryableStatusCodes "
            "error:must be non-empty; "
            "field:methodConfig[0].retryPolicy.retryableStatusCodes[0] "
            "error:is not a string; "
            "field:methodConfig[0].retryPolicy.retryableStatusCodes[1] "
            "error:is not a string]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyUnparsableRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": [\"FOO\", \"BAR\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.retryableStatusCodes "
            "error:must be non-empty; "
            "field:methodConfig[0].retryPolicy.retryableStatusCodes[0] "
            "error:failed to parse status code; "
            "field:methodConfig[0].retryPolicy.retryableStatusCodes[1] "
            "error:failed to parse status code]")
      << service_config.status();
}

TEST_F(RetryParserTest, ValidRetryPolicyWithPerAttemptRecvTimeout) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config = static_cast<internal::RetryMethodConfig*>(
      ((*vector_ptr)[parser_index_]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), Duration::Seconds(1));
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest,
       ValidRetryPolicyWithPerAttemptRecvTimeoutIgnoredWhenHedgingDisabled) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config = static_cast<internal::RetryMethodConfig*>(
      ((*vector_ptr)[parser_index_]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), std::nullopt);
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest,
       ValidRetryPolicyWithPerAttemptRecvTimeoutAndUnsetRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\"\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config = static_cast<internal::RetryMethodConfig*>(
      ((*vector_ptr)[parser_index_]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), Duration::Seconds(1));
  EXPECT_TRUE(parsed_config->retryable_status_codes().Empty());
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutUnparsable) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": \"1sec\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.perAttemptRecvTimeout "
            "error:Not a duration (no s suffix)]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": 1,\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.perAttemptRecvTimeout "
            "error:is not a string]")
      << service_config.status();
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": \"0s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].retryPolicy.perAttemptRecvTimeout "
            "error:must be greater than 0]")
      << service_config.status();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
