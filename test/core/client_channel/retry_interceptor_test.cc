// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/retry_interceptor.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include <optional>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// The retry interceptor is the reason the harness hands out handlers one at a
// time rather than returning one alongside the initiator: a single call from
// the client can turn into any number of child calls down the stack.
class RetryInterceptorTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  absl::Status Init(const ChannelArgs& args = ChannelArgs()) {
    return CreateFilterChain<RetryInterceptor>(args);
  }

  // Arrange for every call to carry a service config with `retry_policy_json`
  // as its retry policy. Without this the interceptor sees no policy and never
  // retries, so exactly one child call is created.
  void SetRetryPolicy(absl::string_view retry_policy_json) {
    SetServiceConfig(absl::StrCat(R"("retryPolicy":)", retry_policy_json));
  }
};

FILTER_TEST(RetryInterceptorTest, NoOp) { ASSERT_TRUE(Init().ok()); }

// With no retry policy configured the interceptor behaves like a filter: one
// call in, one child call out.
FILTER_TEST(RetryInterceptorTest, SingleAttemptSucceeds) {
  ASSERT_TRUE(Init().ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("hello"));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload("hello"));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));

  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

// A retryable failure on the first attempt produces a *second* child call, and
// the client sees only the status of the attempt that succeeded.
FILTER_TEST(RetryInterceptorTest, RetriesOnRetryableStatus) {
  ASSERT_TRUE(Init().ok());
  SetRetryPolicy(R"({
    "maxAttempts": 2,
    "initialBackoff": "0.1s",
    "maxBackoff": "0.1s",
    "backoffMultiplier": 1,
    "retryableStatusCodes": ["UNAVAILABLE"]
  })");
  CallInitiator initiator = StartCall(NewClientMetadata());
  PushClientMessage(initiator, NewMessage("hello"));
  PushClientHalfClose(initiator);

  // First attempt: fails with a retryable status. The interceptor buffers the
  // client message and replays it onto each attempt.
  // FuzzingEventEngine can inject run_delay up to max_delay_run_after (30s)
  // per action. Combined with retry backoff timers (0.1s), child call
  // creation can be scheduled beyond the default 30s timeout of
  // GetNextHandler(). Using a 2-minute timeout allows simulated virtual time
  // to advance past these fuzzed delays without consuming real wall-clock time.
  std::optional<CallHandler> first_attempt =
      GetNextHandler(std::chrono::minutes(2));
  ASSERT_TRUE(first_attempt.has_value());
  ASSERT_TRUE(PullClientInitialMetadata(*first_attempt).ok());
  ClientToServerNextMessage first_message = PullClientMessage(*first_attempt);
  ASSERT_TRUE(first_message.ok());
  ASSERT_TRUE(first_message.has_value());
  EXPECT_THAT(first_message.value(), HasMessagePayload("hello"));
  PushServerTrailingMetadata(
      *first_attempt,
      ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "try again"));

  // Second attempt: the interceptor replays the call onto a new child call.
  std::optional<CallHandler> second_attempt =
      GetNextHandler(std::chrono::minutes(2));
  ASSERT_TRUE(second_attempt.has_value());
  ASSERT_TRUE(PullClientInitialMetadata(*second_attempt).ok());
  ClientToServerNextMessage second_message = PullClientMessage(*second_attempt);
  ASSERT_TRUE(second_message.ok());
  ASSERT_TRUE(second_message.has_value());
  EXPECT_THAT(second_message.value(), HasMessagePayload("hello"));
  PushServerTrailingMetadata(*second_attempt,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));

  EXPECT_TRUE(PullServerTrailingStatus(initiator).ok());

  // One retry, so the interceptor made exactly two child calls.
  EXPECT_EQ(GetNextHandler(), std::nullopt);

  WaitForAllPendingWork();
}

// Once the attempt budget is exhausted the interceptor stops creating child
// calls and surfaces the last failure to the client.
FILTER_TEST(RetryInterceptorTest, GivesUpAfterMaxAttempts) {
  ASSERT_TRUE(Init().ok());
  SetRetryPolicy(R"({
    "maxAttempts": 2,
    "initialBackoff": "0.1s",
    "maxBackoff": "0.1s",
    "backoffMultiplier": 1,
    "retryableStatusCodes": ["UNAVAILABLE"]
  })");
  CallInitiator initiator = StartCall(NewClientMetadata());
  PushClientMessage(initiator, NewMessage("hello"));
  PushClientHalfClose(initiator);

  // Fail each attempt in turn, releasing its handler as a transport would once
  // the call has failed.
  // FuzzingEventEngine can inject run_delay up to max_delay_run_after (30s)
  // per action. Combined with retry backoff timers (0.1s), child call
  // creation can be scheduled beyond the default 30s timeout of
  // GetNextHandler(). Using a 2-minute timeout allows simulated virtual time
  // to advance past these fuzzed delays without consuming real wall-clock time.
  for (int i = 0; i < 2; i++) {
    std::optional<CallHandler> attempt =
        GetNextHandler(std::chrono::minutes(2));
    ASSERT_TRUE(attempt.has_value());
    ASSERT_TRUE(PullClientInitialMetadata(*attempt).ok());
    ClientToServerNextMessage message = PullClientMessage(*attempt);
    ASSERT_TRUE(message.ok());
    ASSERT_TRUE(message.has_value());
    EXPECT_THAT(message.value(), HasMessagePayload("hello"));
    PushServerTrailingMetadata(
        *attempt,
        ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "try again"));
  }

  EXPECT_EQ(PullServerTrailingStatus(initiator),
            absl::UnavailableError("try again"));

  // maxAttempts is 2, so the interceptor stopped after two child calls.
  EXPECT_EQ(GetNextHandler(), std::nullopt);

  WaitForAllPendingWork();
}

// TODO(roth, bpawan): more tests

}  // namespace grpc_core
