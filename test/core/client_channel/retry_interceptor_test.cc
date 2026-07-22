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

#include <utility>
#include <vector>

#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
const absl::string_view kTestPath = "/test_method";
}  // namespace

// The retry interceptor is the reason the harness hands out handlers one at a
// time rather than returning one alongside the initiator: a single call from
// the client can turn into any number of child calls down the stack.
class RetryInterceptorTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  absl::Status Init(const ChannelArgs& args = ChannelArgs()) {
    return InitChannel<RetryInterceptor>(args);
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    ClientMetadataHandle md = NewClientMetadata();
    md->Set(HttpPathMetadata(), Slice::FromCopiedString(kTestPath));
    return md;
  }

  // Arrange for every call to carry a service config with `retry_policy_json`
  // as its retry policy. Without this the interceptor sees no policy and never
  // retries, so exactly one child call is created.
  void SetRetryPolicy(absl::string_view retry_policy_json) {
    absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config =
        ServiceConfigImpl::Create(
            ChannelArgs(),
            absl::StrCat(R"({"methodConfig":[{"name":[{}],"retryPolicy":)",
                         retry_policy_json, "}]}"));
    ASSERT_TRUE(service_config.ok()) << service_config.status();
    service_config_ = std::move(*service_config);
    method_configs_ = service_config_->GetMethodParsedConfigVector(
        Slice::FromCopiedString(kTestPath).c_slice());
  }

  void InitCallArena(Arena* arena) override {
    if (service_config_ == nullptr) return;
    arena->New<ServiceConfigCallData>(arena)->SetServiceConfig(service_config_,
                                                               method_configs_);
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_ = nullptr;
};

FILTER_TEST(RetryInterceptorTest, NoOp) { ASSERT_TRUE(Init().ok()); }

// With no retry policy configured the interceptor behaves like a filter: one
// call in, one child call out.
FILTER_TEST(RetryInterceptorTest, SingleAttemptSucceeds) {
  ASSERT_TRUE(Init().ok());
  auto [initiator, handler] = StartCallForFilter(MakeClientInitialMetadata());

  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// A retryable failure on the first attempt produces a *second* child call, and
// the client sees only the status of the attempt that succeeded. This is the
// case a harness that returned one handler per initiator could not express.
FILTER_TEST(RetryInterceptorTest, RetriesOnRetryableStatus) {
  ASSERT_TRUE(Init().ok());
  SetRetryPolicy(R"({
    "maxAttempts": 2,
    "initialBackoff": "0.1s",
    "maxBackoff": "0.1s",
    "backoffMultiplier": 1,
    "retryableStatusCodes": ["UNAVAILABLE"]
  })");
  StartCall(MakeClientInitialMetadata());
  PushClientHalfClose();

  // First attempt: fails with a retryable status.
  CallHandler first_attempt = GetHandler();
  ASSERT_TRUE(PullClientInitialMetadata(first_attempt).ok());
  PushServerTrailingMetadata(
      first_attempt,
      ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "try again"));

  // Second attempt: the interceptor replays the call onto a new child call.
  CallHandler second_attempt = GetHandler();
  ASSERT_TRUE(PullClientInitialMetadata(second_attempt).ok());
  PushServerTrailingMetadata(second_attempt,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// Once the attempt budget is exhausted the interceptor stops creating child
// calls and surfaces the last failure to the client.
FILTER_TEST(RetryInterceptorTest, GivesUpAfterMaxAttempts) {
  // TODO(pawbhard): investigate flakiness.
  GTEST_SKIP();
  ASSERT_TRUE(Init().ok());
  SetRetryPolicy(R"({
    "maxAttempts": 2,
    "initialBackoff": "0.1s",
    "maxBackoff": "0.1s",
    "backoffMultiplier": 1,
    "retryableStatusCodes": ["UNAVAILABLE"]
  })");
  StartCall(MakeClientInitialMetadata());
  PushClientHalfClose();

  // Keep each attempt's handler alive for the whole test, as a transport
  // would.
  std::vector<CallHandler> attempts;
  for (int i = 0; i < 2; i++) {
    attempts.push_back(GetHandler());
    ASSERT_TRUE(PullClientInitialMetadata(attempts.back()).ok());
    PushServerTrailingMetadata(
        attempts.back(),
        ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "try again"));
  }

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata,
              HasMetadataResult(absl::UnavailableError("try again")));

  WaitForAllPendingWork();
}

}  // namespace grpc_core
