//
//
// Copyright 2015 gRPC authors.
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
//

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <stdio.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

class LargeMetadataTest {
 public:
  LargeMetadataTest(CoreEnd2endTest& test, const ChannelArgs& args)
      : test_(test) {
    test_.InitClient(args);
    test_.InitServer(args);
  }

  int PerformRequests(size_t metadata_size, int count) {
    int num_requests_accepted = 0;
    for (int i = 0; i < count; ++i) {
      auto status = PerformOneRequest(metadata_size);
      if (status.status() == GRPC_STATUS_RESOURCE_EXHAUSTED) {
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("received metadata size exceeds"));
      } else {
        num_requests_accepted++;
        EXPECT_EQ(status.status(), GRPC_STATUS_OK);
        EXPECT_EQ(status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
      }
    }
    return num_requests_accepted;
  }

 private:
  IncomingStatusOnClient PerformOneRequest(const size_t metadata_size) {
    auto c = test_.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
    IncomingMetadata server_initial_metadata;
    IncomingStatusOnClient server_status;
    c.NewBatch(1)
        .SendInitialMetadata({})
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    auto s = test_.RequestCall(101);
    test_.Expect(101, true);
    test_.Step();
    // Server: send metadata of size `metadata_size`.
    IncomingCloseOnServer client_close;
    s.NewBatch(102)
        .SendInitialMetadata({{"key", std::string(metadata_size, 'a')}})
        .RecvCloseOnServer(client_close)
        .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
    test_.Expect(102, true);
    test_.Expect(1, true);
    test_.Step();
    return server_status;
  }

  CoreEnd2endTest& test_;
};

// Server responds with metadata under soft limit of what client accepts. No
// requests should be rejected.
CORE_END2END_TEST(Http2SingleHopTests, RequestWithLargeMetadataUnderSoftLimit) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = soft_limit;
  LargeMetadataTest test(
      *this, ChannelArgs()
                 .Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024)
                 .Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE, hard_limit + 1024));
  EXPECT_EQ(test.PerformRequests(metadata_size, 100), 100);
}

// Server responds with metadata between soft and hard limits of what client
// accepts. Some requests should be rejected.
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataBetweenSoftAndHardLimits) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = (soft_limit + hard_limit) / 2;
  LargeMetadataTest test(
      *this, ChannelArgs()
                 .Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024)
                 .Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE, hard_limit + 1024));
  EXPECT_THAT(test.PerformRequests(metadata_size, 100),
              ::testing::AllOf(::testing::Ge(5), ::testing::Le(95)));
}

// Server responds with metadata above hard limit of what the client accepts.
// All requests should be rejected.
CORE_END2END_TEST(Http2SingleHopTests, RequestWithLargeMetadataAboveHardLimit) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = hard_limit * 3 / 2;
  LargeMetadataTest test(
      *this, ChannelArgs()
                 .Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024)
                 .Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE, hard_limit + 1024));
  EXPECT_EQ(test.PerformRequests(metadata_size, 100), 0);
}

// Set soft limit higher than hard limit. All requests above hard limit should
// be rejected, all requests below hard limit should be accepted (soft limit
// should not be respected).
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataSoftLimitAboveHardLimit) {
  const size_t soft_limit = 64 * 1024;
  const size_t hard_limit = 32 * 1024;
  const size_t metadata_size_below_hard_limit = hard_limit;
  const size_t metadata_size_above_hard_limit = hard_limit * 2;
  LargeMetadataTest test(
      *this, ChannelArgs()
                 .Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024)
                 .Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE, hard_limit + 1024));
  // Send 50 requests below hard limit. Should be accepted.
  EXPECT_EQ(test.PerformRequests(metadata_size_below_hard_limit, 50), 50);
  // Send 50 requests above hard limit. Should be rejected.
  EXPECT_EQ(test.PerformRequests(metadata_size_above_hard_limit, 50), 0);
}

// Set soft limit * 1.25 higher than default hard limit and do not set hard
// limit. Soft limit * 1.25 should be used as hard limit.
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataSoftLimitOverridesDefaultHard) {
  const size_t soft_limit = 64 * 1024;
  const size_t metadata_size_below_soft_limit = soft_limit;
  const size_t metadata_size_above_hard_limit = soft_limit * 1.5;
  const size_t metadata_size_between_limits =
      (soft_limit + soft_limit * 1.25) / 2;
  LargeMetadataTest test(
      *this, ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024));
  // Send 50 requests below soft limit. Should be accepted.
  EXPECT_EQ(test.PerformRequests(metadata_size_below_soft_limit, 50), 50);
  // Send 100 requests between soft and hard limits. Some should be rejected.
  EXPECT_THAT(test.PerformRequests(metadata_size_between_limits, 100),
              ::testing::AllOf(::testing::Ge(5), ::testing::Le(95)));
  // Send 50 requests above hard limit. Should be rejected.
  EXPECT_EQ(test.PerformRequests(metadata_size_above_hard_limit, 50), 0);
}

// Set hard limit * 0.8 higher than default soft limit and do not set soft
// limit. Hard limit * 0.8 should be used as soft limit.
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataHardLimitOverridesDefaultSoft) {
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size_below_soft_limit = hard_limit * 0.5;
  const size_t metadata_size_above_hard_limit = hard_limit * 1.5;
  const size_t metadata_size_between_limits =
      (hard_limit * 0.8 + hard_limit) / 2;
  LargeMetadataTest test(*this,
                         ChannelArgs().Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE,
                                           hard_limit + 1024));
  // Send 50 requests below soft limit. Should be accepted.
  EXPECT_EQ(test.PerformRequests(metadata_size_below_soft_limit, 50), 50);
  // Send 100 requests between soft and hard limits. Some should be rejected.
  EXPECT_THAT(test.PerformRequests(metadata_size_between_limits, 100),
              ::testing::AllOf(::testing::Ge(5), ::testing::Le(95)));
  // Send 50 requests above hard limit. Should be rejected.
  EXPECT_EQ(test.PerformRequests(metadata_size_above_hard_limit, 50), 0);
}

// Set hard limit lower than default hard limit and ensure new limit is
// respected. Default soft limit is not respected since hard limit is lower than
// soft limit.
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataHardLimitBelowDefaultHard) {
  const size_t hard_limit = 4 * 1024;
  const size_t metadata_size_below_hard_limit = hard_limit;
  const size_t metadata_size_above_hard_limit = hard_limit * 2;
  LargeMetadataTest test(*this,
                         ChannelArgs().Set(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE,
                                           hard_limit + 1024));
  // Send 50 requests below hard limit. Should be accepted.
  EXPECT_EQ(test.PerformRequests(metadata_size_below_hard_limit, 50), 50);
  // Send 50 requests above hard limit. Should be rejected.
  EXPECT_EQ(test.PerformRequests(metadata_size_above_hard_limit, 50), 0);
}

// Set soft limit lower than default soft limit and ensure new limit is
// respected. Hard limit should be default hard since this is greater than 2 *
// soft limit.
CORE_END2END_TEST(Http2SingleHopTests,
                  RequestWithLargeMetadataSoftLimitBelowDefaultSoft) {
  const size_t soft_limit = 1 * 1024;
  const size_t metadata_size_below_soft_limit = soft_limit;
  // greater than 2 * soft, less than default hard
  const size_t metadata_size_between_limits = 10 * 1024;
  const size_t metadata_size_above_hard_limit = 75 * 1024;
  LargeMetadataTest test(
      *this, ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, soft_limit + 1024));
  // Send 50 requests below soft limit. Should be accepted.
  EXPECT_EQ(test.PerformRequests(metadata_size_below_soft_limit, 50), 50);
  // Send 100 requests between soft and hard limits. Some should be rejected.
  EXPECT_THAT(test.PerformRequests(metadata_size_between_limits, 100),
              ::testing::AllOf(::testing::Ge(1), ::testing::Le(99)));
  // Send 50 requests above hard limit. Should be rejected.
  EXPECT_EQ(test.PerformRequests(metadata_size_above_hard_limit, 50), 0);
}

}  // namespace
}  // namespace grpc_core
