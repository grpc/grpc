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

LoadBalancerStatsResponse::MetadataByPeer BuildMetadatas(
    const std::initializer_list<
        std::initializer_list<std::pair<std::string, std::string>>>& values) {
  LoadBalancerStatsResponse::MetadataByPeer metadata_by_peer;
  for (const auto& per_rpc : values) {
    auto rpc_metadata = metadata_by_peer.add_rpc_metadata();
    for (const auto& key_value : per_rpc) {
      auto entry = rpc_metadata->add_metadata();
      entry->set_key(key_value.first);
      entry->set_value(key_value.second);
    }
  }
  return metadata_by_peer;
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponse) {
  // "k3" will be ignored
  XdsStatsWatcher watcher(0, 3, {"k1", "k2"});
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"k1", "v1"}, {"k2", "v2"}, {"k3", "v3"}});
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {{"k1", "v4"}});
  watcher.RpcCompleted(BuildCallResult(2), "peer2",
                       {{"k1", "v5"}, {"k2", "v6"}, {"k3", "v7"}});
  LoadBalancerStatsResponse expected;
  expected.mutable_rpcs_by_peer()->insert({{"peer1", 2}, {"peer2", 1}});
  expected.mutable_metadatas_by_peer()->insert({
      {"peer1", BuildMetadatas({{{"k1", "v1"}, {"k2", "v2"}}, {{"k1", "v4"}}})},
      {"peer2", BuildMetadatas({{{"k1", "v5"}, {"k2", "v6"}}})},
  });
  (*expected.mutable_rpcs_by_method())["UnaryCall"]
      .mutable_rpcs_by_peer()
      ->insert({{"peer1", 2}, {"peer2", 1}});
  EXPECT_EQ(watcher.WaitForRpcStatsResponse(0).DebugString(),
            expected.DebugString());
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponse_IgnoresMetadata) {
  XdsStatsWatcher watcher(0, 3, {});
  // RPC had metadata - but watcher should ignore it
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"k1", "v1"}, {"k2", "v2"}, {"k3", "v3"}});
  // No metadata came with RPC
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {});
  watcher.RpcCompleted(BuildCallResult(2), "peer2",
                       {{"k1", "v5"}, {"k2", "v6"}, {"k3", "v7"}});
  LoadBalancerStatsResponse expected;
  expected.mutable_rpcs_by_peer()->insert({{"peer1", 2}, {"peer2", 1}});
  // There will still be an empty metadata collection for each RPC
  expected.mutable_metadatas_by_peer()->insert({
      {"peer1", BuildMetadatas({{}, {}})},
      {"peer2", BuildMetadatas({{}})},
  });
  (*expected.mutable_rpcs_by_method())["UnaryCall"]
      .mutable_rpcs_by_peer()
      ->insert({{"peer1", 2}, {"peer2", 1}});
  EXPECT_EQ(watcher.WaitForRpcStatsResponse(0).DebugString(),
            expected.DebugString());
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
