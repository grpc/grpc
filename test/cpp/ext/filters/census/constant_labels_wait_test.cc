//
//
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
//
//

#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/stats/stats.h"
#include "opencensus/stats/testing/test_utils.h"
#include "opencensus/tags/tag_map.h"

#include <grpc++/grpc++.h>
#include <grpcpp/opencensus.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/ext/filters/census/library.h"

namespace grpc {
namespace testing {

namespace {

using ::opencensus::stats::View;
using ::opencensus::stats::testing::TestUtils;

class ConstantLabelsWaitTest : public StatsPluginEnd2EndTest {
 protected:
  static void SetUpTestSuite() {
    grpc::internal::OpenCensusRegistry::Get().RegisterWaitOnReady();
    grpc::internal::EnableOpenCensusTracing(false);
    StatsPluginEnd2EndTest::SetUpTestSuite();
  }
};

// Check that RPCs wait for labels to be registered to OpenCensus.
TEST_F(ConstantLabelsWaitTest, RpcWaitsForLabelsRegistration) {
  View* client_completed_rpcs_view = nullptr;
  View* server_completed_rpcs_view = nullptr;

  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;

  {
    grpc::ClientContext context;
    grpc_core::Notification notification;
    stub_->async()->Echo(&context, &request, &response,
                         [&notification, &response](Status s) mutable {
                           EXPECT_TRUE(s.ok());
                           notification.Notify();
                           EXPECT_EQ("foo", response.message());
                         });
    // Introduce a sleep to check that the RPC waits for labels to be
    // registered.
    absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
    grpc::internal::OpenCensusRegistry::Get().RegisterConstantLabels(
        {{"key", "value"}});
    client_completed_rpcs_view = new View(ClientCompletedRpcsCumulative());
    server_completed_rpcs_view = new View(ServerCompletedRpcsCumulative());
    {
      grpc_core::ExecCtx exec_ctx;
      grpc::internal::OpenCensusRegistry::Get().SetReady();
    }
    notification.WaitForNotification();
  }

  absl::SleepFor(absl::Milliseconds(500 * grpc_test_slowdown_factor()));
  TestUtils::Flush();

  EXPECT_THAT(
      client_completed_rpcs_view->GetData().int_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre("value", client_method_name_, "OK"), 1)));
  EXPECT_THAT(
      server_completed_rpcs_view->GetData().int_data(),
      ::testing::UnorderedElementsAre(::testing::Pair(
          ::testing::ElementsAre("value", server_method_name_, "OK"), 1)));
  delete client_completed_rpcs_view;
  delete server_completed_rpcs_view;
}

}  // namespace

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
