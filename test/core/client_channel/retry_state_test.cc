// Copyright 2023 gRPC authors.
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

// Test to verify Fuzztest integration

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/retry_interceptor.h"
#include "src/core/util/json/json_reader.h"

using fuzztest::Arbitrary;
using fuzztest::ElementOf;
using fuzztest::InRange;
using fuzztest::InRegexp;
using fuzztest::Just;
using fuzztest::OneOf;
using fuzztest::OptionalOf;
using fuzztest::PairOf;
using fuzztest::VariantOf;
using fuzztest::VectorOf;

namespace grpc_core {
namespace retry_detail {
namespace {

// Domain that includes any duration possible
auto AnyDuration() {
  return fuzztest::Map(
      [](int64_t millis) { return Duration::Milliseconds(millis); },
      Arbitrary<int64_t>());
}

// Domain that includes only negative durations
auto NegativeDuration() {
  return fuzztest::Map(
      [](int64_t millis) { return Duration::Milliseconds(millis); },
      fuzztest::Negative<int64_t>());
}

// Domain that includes valid grpc_status_codes
auto AnyStatus() {
  return ElementOf({
      GRPC_STATUS_OK,
      GRPC_STATUS_CANCELLED,
      GRPC_STATUS_UNKNOWN,
      GRPC_STATUS_INVALID_ARGUMENT,
      GRPC_STATUS_DEADLINE_EXCEEDED,
      GRPC_STATUS_NOT_FOUND,
      GRPC_STATUS_ALREADY_EXISTS,
      GRPC_STATUS_PERMISSION_DENIED,
      GRPC_STATUS_RESOURCE_EXHAUSTED,
      GRPC_STATUS_FAILED_PRECONDITION,
      GRPC_STATUS_ABORTED,
      GRPC_STATUS_OUT_OF_RANGE,
      GRPC_STATUS_UNIMPLEMENTED,
      GRPC_STATUS_INTERNAL,
      GRPC_STATUS_UNAVAILABLE,
      GRPC_STATUS_DATA_LOSS,
  });
}

// Any status not including x
auto AnyStatusExcept(grpc_status_code x) {
  std::vector<grpc_status_code> status_codes({
      GRPC_STATUS_OK,
      GRPC_STATUS_CANCELLED,
      GRPC_STATUS_UNKNOWN,
      GRPC_STATUS_INVALID_ARGUMENT,
      GRPC_STATUS_DEADLINE_EXCEEDED,
      GRPC_STATUS_NOT_FOUND,
      GRPC_STATUS_ALREADY_EXISTS,
      GRPC_STATUS_PERMISSION_DENIED,
      GRPC_STATUS_RESOURCE_EXHAUSTED,
      GRPC_STATUS_FAILED_PRECONDITION,
      GRPC_STATUS_ABORTED,
      GRPC_STATUS_OUT_OF_RANGE,
      GRPC_STATUS_UNIMPLEMENTED,
      GRPC_STATUS_INTERNAL,
      GRPC_STATUS_UNAVAILABLE,
  });
  status_codes.erase(std::remove(status_codes.begin(), status_codes.end(), x),
                     status_codes.end());
  return ElementOf(status_codes);
}

// Domain that includes any metadata (biased to what's useful for these tests)
auto AnyServerMetadata() {
  return fuzztest::Map(
      [](std::optional<grpc_status_code> status,
         std::optional<Duration> pushback,
         std::vector<std::pair<std::string, std::string>> vec) {
        auto md = Arena::MakePooled<ServerMetadata>();
        for (const auto& [key, value] : vec) {
          md->Append(key, Slice::FromCopiedString(value),
                     [](absl::string_view, const Slice&) {});
        }
        if (status.has_value()) {
          md->Set(GrpcStatusMetadata(), *status);
        } else {
          md->Remove(GrpcStatusMetadata());
        }
        if (pushback.has_value()) {
          md->Set(GrpcRetryPushbackMsMetadata(), *pushback);
        } else {
          md->Remove(GrpcRetryPushbackMsMetadata());
        }
        return md;
      },
      OptionalOf(AnyStatus()), OptionalOf(AnyDuration()),
      VectorOf(PairOf(InRegexp("[a-z0-9]*"), InRegexp("[a-z0-9]*"))));
}

// Domain that includes any metadata with a particular status
template <typename AllowedStatus>
auto ServerMetadataWithStatus(AllowedStatus status) {
  return fuzztest::Map(
      [](grpc_status_code status, std::optional<Duration> pushback,
         std::vector<std::pair<std::string, std::string>> vec) {
        auto md = Arena::MakePooled<ServerMetadata>();
        for (const auto& [key, value] : vec) {
          md->Append(key, Slice::FromCopiedString(value),
                     [](absl::string_view, const Slice&) {});
        }
        md->Set(GrpcStatusMetadata(), status);
        if (pushback.has_value()) {
          md->Set(GrpcRetryPushbackMsMetadata(), *pushback);
        } else {
          md->Remove(GrpcRetryPushbackMsMetadata());
        }
        return md;
      },
      status, OptionalOf(AnyDuration()),
      VectorOf(PairOf(InRegexp("[a-z0-9]*"), InRegexp("[a-z0-9]*"))));
}

// Domain that includes all metadata with the specified pushback
template <typename AllowedPushback>
auto ServerMetadataWithPushback(AllowedPushback pushback) {
  return fuzztest::Map(
      [](grpc_status_code status, Duration pushback,
         std::vector<std::pair<std::string, std::string>> vec) {
        auto md = Arena::MakePooled<ServerMetadata>();
        for (const auto& [key, value] : vec) {
          md->Append(key, Slice::FromCopiedString(value),
                     [](absl::string_view, const Slice&) {});
        }
        md->Set(GrpcStatusMetadata(), status);
        md->Set(GrpcRetryPushbackMsMetadata(), pushback);
        return md;
      },
      AnyStatus(), pushback,
      VectorOf(PairOf(InRegexp("[a-z0-9]*"), InRegexp("[a-z0-9]*"))));
}

// Domain that includes all successful request metadata
auto AnySuccessfulMetadata() {
  return ServerMetadataWithStatus(ElementOf({GRPC_STATUS_OK}));
}

auto SomeServerThrottleData() {
  return fuzztest::Map(
      [](uintptr_t max_milli_tokens, uintptr_t milli_token_ratio) {
        return MakeRefCounted<internal::ServerRetryThrottleData>(
            max_milli_tokens, milli_token_ratio, max_milli_tokens);
      },
      Arbitrary<uintptr_t>(), Arbitrary<uintptr_t>());
}

auto AnyServerThrottleData() {
  return OneOf(Just(RefCountedPtr<internal::ServerRetryThrottleData>()),
               SomeServerThrottleData());
}

// Helper to get the debug tag passed to ShouldRetry correct
std::string FuzzerDebugTag() { return "fuzzer"; }

// Construct a policy from json text
internal::RetryMethodConfig MakePolicy(absl::string_view json) {
  auto json_obj = JsonParse(json);
  CHECK_OK(json_obj) << json;
  auto obj = LoadFromJson<internal::RetryMethodConfig>(*json_obj);
  CHECK_OK(obj) << json;
  return std::move(*obj);
}

// Domain including valid retry configurations
auto AnyRetryMethodConfig() {
  return fuzztest::Map(
      [](uint32_t max_attempts, uint32_t initial_backoff, uint32_t max_backoff,
         double backoff_multiplier,
         std::vector<grpc_status_code> retryable_status_codes,
         std::variant<uint32_t, grpc_status_code>
             per_attempt_recv_timeout_or_another_retriable_status_code) {
        std::optional<uint32_t> per_attempt_recv_timeout;
        if (std::holds_alternative<grpc_status_code>(
                per_attempt_recv_timeout_or_another_retriable_status_code)) {
          retryable_status_codes.push_back(std::get<grpc_status_code>(
              per_attempt_recv_timeout_or_another_retriable_status_code));
        } else {
          per_attempt_recv_timeout = std::get<uint32_t>(
              per_attempt_recv_timeout_or_another_retriable_status_code);
        }
        std::sort(retryable_status_codes.begin(), retryable_status_codes.end());
        retryable_status_codes.erase(std::unique(retryable_status_codes.begin(),
                                                 retryable_status_codes.end()),
                                     retryable_status_codes.end());
        return MakePolicy(absl::StrCat(
            "{\"maxAttempts\":", max_attempts, ",\"initialBackoff\":\"",
            Duration::Milliseconds(initial_backoff).ToJsonString(),
            "\",\"maxBackoff\":\"",
            Duration::Milliseconds(max_backoff).ToJsonString(),
            "\",\"backoffMultiplier\":", backoff_multiplier,
            ",\"retryableStatusCodes\":[",
            absl::StrJoin(retryable_status_codes, ",",
                          [](std::string* out, grpc_status_code c) {
                            absl::StrAppend(
                                out, "\"", grpc_status_code_to_string(c), "\"");
                          }),
            "]",
            per_attempt_recv_timeout.has_value()
                ? absl::StrCat(",\"perAttemptRecvTimeout\":\"",
                               Duration::Milliseconds(*per_attempt_recv_timeout)
                                   .ToJsonString(),
                               "\"")
                : "",
            "}"));
      },
      InRange(2, 5), InRange(1, 100000), InRange(1, 100000),
      InRange(1e-12, 10.0), VectorOf(AnyStatus()),
      VariantOf(InRange<uint32_t>(1, 100000), AnyStatus()));
}

// Domain including valid retry configurations that only retry particular status
// codes
auto RetryMethodConfigWithRetryableStatusCodes(
    std::vector<grpc_status_code> retryable_status_codes) {
  return fuzztest::Map(
      [retryable_status_codes](uint32_t max_attempts, uint32_t initial_backoff,
                               uint32_t max_backoff,
                               double backoff_multiplier) {
        return MakePolicy(absl::StrCat(
            "{\"maxAttempts\":", max_attempts, ",\"initialBackoff\":\"",
            Duration::Milliseconds(initial_backoff).ToJsonString(),
            "\",\"maxBackoff\":\"",
            Duration::Milliseconds(max_backoff).ToJsonString(),
            "\",\"backoffMultiplier\":", backoff_multiplier,
            ",\"retryableStatusCodes\":[",
            absl::StrJoin(retryable_status_codes, ",",
                          [](std::string* out, grpc_status_code c) {
                            absl::StrAppend(
                                out, "\"", grpc_status_code_to_string(c), "\"");
                          }),
            "]}"));
      },
      InRange(2, 5), InRange(1, 100000), InRange(1, 100000),
      InRange(1e-12, 10.0));
}

void Printable(std::optional<internal::RetryMethodConfig> policy,
               RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(policy.has_value() ? &*policy : nullptr,
                         throttle_data);
  std::ignore = absl::StrCat(retry_state);
}
FUZZ_TEST(MyTestSuite, Printable)
    .WithDomains(OptionalOf(AnyRetryMethodConfig()), AnyServerThrottleData());

void NoPolicyNeverRetries(std::vector<ServerMetadataHandle> md,
                          bool committed_at_end) {
  RetryState retry_state(nullptr, nullptr);
  for (auto it = md.begin(); it != md.end(); ++it) {
    EXPECT_EQ(
        retry_state.ShouldRetry(**it, (it + 1 == md.end() && committed_at_end),
                                FuzzerDebugTag),
        std::nullopt);
  }
}
FUZZ_TEST(MyTestSuite, NoPolicyNeverRetries)
    .WithDomains(VectorOf(AnyServerMetadata()), Arbitrary<bool>());

void SuccessfulRequestsNeverRetry(
    internal::RetryMethodConfig policy, ServerMetadataHandle md, bool committed,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(&policy, throttle_data);
  EXPECT_EQ(retry_state.ShouldRetry(*md, committed, FuzzerDebugTag),
            std::nullopt);
}
FUZZ_TEST(MyTestSuite, SuccessfulRequestsNeverRetry)
    .WithDomains(AnyRetryMethodConfig(), AnySuccessfulMetadata(),
                 Arbitrary<bool>(), AnyServerThrottleData());

void CommittedRequestsNeverRetry(
    internal::RetryMethodConfig policy, ServerMetadataHandle md,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(&policy, throttle_data);
  EXPECT_EQ(retry_state.ShouldRetry(*md, true, FuzzerDebugTag), std::nullopt);
}
FUZZ_TEST(MyTestSuite, CommittedRequestsNeverRetry)
    .WithDomains(AnyRetryMethodConfig(), AnyServerMetadata(),
                 AnyServerThrottleData());

void NonRetryableRequestsNeverRetry(
    internal::RetryMethodConfig policy, ServerMetadataHandle md, bool committed,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(&policy, throttle_data);
  EXPECT_EQ(retry_state.ShouldRetry(*md, committed, FuzzerDebugTag),
            std::nullopt);
}
FUZZ_TEST(MyTestSuite, NonRetryableRequestsNeverRetry)
    .WithDomains(
        RetryMethodConfigWithRetryableStatusCodes({GRPC_STATUS_ABORTED}),
        ServerMetadataWithStatus(AnyStatusExcept(GRPC_STATUS_ABORTED)),
        Arbitrary<bool>(), AnyServerThrottleData());

void NeverExceedMaxAttempts(
    internal::RetryMethodConfig policy, std::vector<ServerMetadataHandle> md,
    bool committed_at_end,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(&policy, nullptr);
  int attempts_completed = 0;
  for (auto it = md.begin(); it != md.end(); ++it) {
    ++attempts_completed;
    if (retry_state.ShouldRetry(**it, (it + 1 == md.end() && committed_at_end),
                                FuzzerDebugTag) == std::nullopt) {
      break;
    }
  }
  EXPECT_LE(attempts_completed, policy.max_attempts());
}
FUZZ_TEST(MyTestSuite, NeverExceedMaxAttempts)
    .WithDomains(AnyRetryMethodConfig(),
                 VectorOf(AnyServerMetadata()).WithMaxSize(7),
                 Arbitrary<bool>(), AnyServerThrottleData());

void NeverRetryNegativePushback(
    internal::RetryMethodConfig policy, ServerMetadataHandle md, bool committed,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  RetryState retry_state(&policy, nullptr);
  EXPECT_EQ(retry_state.ShouldRetry(*md, committed, FuzzerDebugTag),
            std::nullopt);
}
FUZZ_TEST(MyTestSuite, NeverRetryNegativePushback)
    .WithDomains(AnyRetryMethodConfig(),
                 ServerMetadataWithPushback(NegativeDuration()),
                 Arbitrary<bool>(), AnyServerThrottleData());

void NeverExceedMaxBackoff(
    internal::RetryMethodConfig policy, std::vector<ServerMetadataHandle> mds,
    RefCountedPtr<internal::ServerRetryThrottleData> throttle_data) {
  if (!IsBackoffCapInitialAtMaxEnabled()) return;
  RetryState retry_state(&policy, nullptr);
  for (const auto& md : mds) {
    auto delay = retry_state.ShouldRetry(*md, false, FuzzerDebugTag);
    if (!delay.has_value()) return;
    EXPECT_GE(delay, Duration::Zero());
    Duration max_delay = policy.max_backoff() * 1.2;
    if (auto pushback = md->get(GrpcRetryPushbackMsMetadata());
        pushback.has_value()) {
      max_delay = std::max(max_delay, *pushback);
    }
    EXPECT_LE(delay, max_delay)
        << " md:" << md->DebugString() << " policy:" << absl::StrCat(policy);
  }
}
FUZZ_TEST(MyTestSuite, NeverExceedMaxBackoff)
    .WithDomains(AnyRetryMethodConfig(), VectorOf(AnyServerMetadata()),
                 AnyServerThrottleData());

}  // namespace
}  // namespace retry_detail
}  // namespace grpc_core
