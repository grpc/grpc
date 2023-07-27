//
// Copyright 2017 gRPC authors.
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

#include <atomic>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_lb_policies.h"

namespace grpc_core {
namespace {
std::atomic<int> g_num_lb_picks;

// Tests that we retry properly when the LB policy fails the call before
// it ever gets to the transport, even if recv_trailing_metadata isn't
// started by the application until after the LB pick fails.
// - 1 retry allowed for ABORTED status
// - on first attempt, LB policy fails with ABORTED before application
//   starts recv_trailing_metadata op
CORE_END2END_TEST(RetryTest, RetryLbFail) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    RegisterFailLoadBalancingPolicy(
        builder, absl::UnavailableError("LB pick failed"), &g_num_lb_picks);
  });
  g_num_lb_picks.store(0, std::memory_order_relaxed);
  InitServer(ChannelArgs());
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_ENABLE_RETRIES, true)
          .Set(GRPC_ARG_SERVICE_CONFIG,
               "{\n"
               "  \"loadBalancingConfig\": [ {\n"
               "    \"fail_lb\": {}\n"
               "  } ],\n"
               "  \"methodConfig\": [ {\n"
               "    \"name\": [\n"
               "      { \"service\": \"service\", \"method\": \"method\" }\n"
               "    ],\n"
               "    \"retryPolicy\": {\n"
               "      \"maxAttempts\": 2,\n"
               "      \"initialBackoff\": \"1s\",\n"
               "      \"maxBackoff\": \"120s\",\n"
               "      \"backoffMultiplier\": 1.6,\n"
               "      \"retryableStatusCodes\": [ \"UNAVAILABLE\" ]\n"
               "    }\n"
               "  } ]\n"
               "}"));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  c.NewBatch(1).SendInitialMetadata({});
  Expect(1, false);
  Step();
  IncomingStatusOnClient server_status;
  c.NewBatch(2).RecvStatusOnClient(server_status);
  Expect(2, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(server_status.message(), "LB pick failed");
  EXPECT_EQ(g_num_lb_picks.load(std::memory_order_relaxed), 2);
}

}  // namespace
}  // namespace grpc_core
