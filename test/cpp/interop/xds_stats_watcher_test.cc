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

#include "test/cpp/interop/xds_stats_watcher.h"

#include <map>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {
AsyncClientCallResult BuildCallResult(int saved_request_id) {
  AsyncClientCallResult result;
  result.saved_request_id = saved_request_id;
  result.rpc_type = ClientConfigureRequest::UNARY_CALL;
  return result;
}

TEST(XdsStatsWatcherTest, CollectsMetadata) {
  XdsStatsWatcher watcher(0, 3);
  watcher.RpcCompleted(BuildCallResult(0), "peer1");
  watcher.RpcCompleted(BuildCallResult(1), "peer1");
  watcher.RpcCompleted(BuildCallResult(2), "peer2");
  LoadBalancerStatsResponse lb_response;
  watcher.WaitForRpcStatsResponse(&lb_response, 1);
  EXPECT_EQ(
      (std::multimap<std::string, int32_t>(lb_response.rpcs_by_peer().begin(),
                                           lb_response.rpcs_by_peer().end())),
      (std::multimap<std::string, int32_t>({{"peer1", 2}, {"peer2", 1}})));
  EXPECT_EQ(lb_response.rpcs_by_method_size(), 1);
  auto rpcs = lb_response.rpcs_by_method().find("UnaryCall");
  EXPECT_NE(rpcs, lb_response.rpcs_by_method().end());
  std::multimap<std::string, int32_t> by_peer(
      rpcs->second.rpcs_by_peer().begin(), rpcs->second.rpcs_by_peer().end());
  EXPECT_EQ(
      by_peer,
      (std::multimap<std::string, int32_t>({{"peer1", 2}, {"peer2", 1}})));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
