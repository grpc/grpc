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

struct MetadataEntryInit {
  absl::string_view key;
  absl::string_view value;
  bool is_trailing;
};

LoadBalancerStatsResponse::MetadataByPeer BuildMetadatas(
    const std::initializer_list<std::initializer_list<MetadataEntryInit>>&
        values) {
  LoadBalancerStatsResponse::MetadataByPeer metadata_by_peer;
  for (const auto& per_rpc : values) {
    auto rpc_metadata = metadata_by_peer.add_rpc_metadata();
    for (const auto& key_value : per_rpc) {
      auto entry = rpc_metadata->add_metadata();
      entry->set_key(key_value.key);
      entry->set_value(key_value.value);
      entry->set_type(key_value.is_trailing
                          ? LoadBalancerStatsResponse::TRAILING
                          : LoadBalancerStatsResponse::INITIAL);
    }
  }
  return metadata_by_peer;
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponse) {
  // "k3" will be ignored
  XdsStatsWatcher watcher(0, 4, {"k1", "k2"});
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"k1", "v1"}, {"k2", "v2"}, {"k3", "v3"}},
                       {{"k1", "t1"}, {"k3", "t3"}});
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {{"k1", "v4"}}, {});
  watcher.RpcCompleted(BuildCallResult(2), "peer1", {}, {});
  watcher.RpcCompleted(BuildCallResult(3), "peer2",
                       {{"k1", "v5"}, {"k2", "v6"}, {"k3", "v7"}},
                       {{"k1", "t5"}, {"k3", "t7"}});
  LoadBalancerStatsResponse expected;
  expected.mutable_rpcs_by_peer()->insert({{"peer1", 3}, {"peer2", 1}});
  expected.mutable_metadatas_by_peer()->insert({
      {"peer1",
       BuildMetadatas({
           {{"k1", "v1", false}, {"k2", "v2", false}, {"k1", "t1", true}},
           {{"k1", "v4", false}},
           {},
       })},
      {"peer2",
       BuildMetadatas({
           {{"k1", "v5", false}, {"k2", "v6", false}, {"k1", "t5", true}},
       })},
  });
  (*expected.mutable_rpcs_by_method())["UnaryCall"]
      .mutable_rpcs_by_peer()
      ->insert({{"peer1", 3}, {"peer2", 1}});
  EXPECT_EQ(expected.DebugString(),
            watcher.WaitForRpcStatsResponse(0).DebugString());
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponseIgnoresCase) {
  // "k3" will be ignored
  XdsStatsWatcher watcher(0, 3, {"k1", "K2"});
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"K1", "v1"}, {"k2", "v2"}, {"k3", "v3"}},
                       {{"K1", "t1"}, {"k2", "t2"}});
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {}, {});
  watcher.RpcCompleted(BuildCallResult(2), "peer2", {},
                       {{"k1", "v5"}, {"K2", "v6"}, {"k3", "v7"}});
  LoadBalancerStatsResponse expected;
  expected.mutable_rpcs_by_peer()->insert({{"peer1", 2}, {"peer2", 1}});
  expected.mutable_metadatas_by_peer()->insert({
      {"peer1", BuildMetadatas({
                    {{"K1", "v1", false},
                     {"k2", "v2", false},
                     {"K1", "t1", true},
                     {"k2", "t2", true}},
                    {},
                })},
      {"peer2", BuildMetadatas({{{"K2", "v6", true}, {"k1", "v5", true}}})},
  });
  (*expected.mutable_rpcs_by_method())["UnaryCall"]
      .mutable_rpcs_by_peer()
      ->insert({{"peer1", 2}, {"peer2", 1}});
  EXPECT_EQ(expected.DebugString(),
            watcher.WaitForRpcStatsResponse(0).DebugString());
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponseReturnsAll) {
  // "k3" will be ignored
  XdsStatsWatcher watcher(0, 3, {"*"});
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"K1", "v1"}, {"k2", "v2"}, {"k3", "v3"}},
                       {{"K1", "t1"}, {"k2", "t2"}});
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {}, {});
  watcher.RpcCompleted(BuildCallResult(2), "peer2", {},
                       {{"k1", "v5"}, {"K2", "v6"}, {"k3", "v7"}});
  LoadBalancerStatsResponse expected;
  expected.mutable_rpcs_by_peer()->insert({{"peer1", 2}, {"peer2", 1}});
  expected.mutable_metadatas_by_peer()->insert({
      {"peer1", BuildMetadatas({
                    {{"K1", "v1", false},
                     {"k2", "v2", false},
                     {"k3", "v3", false},
                     {"K1", "t1", true},
                     {"k2", "t2", true}},
                    {},
                })},
      {"peer2",
       BuildMetadatas(
           {{{"K2", "v6", true}, {"k1", "v5", true}, {"k3", "v7", true}}})},
  });
  (*expected.mutable_rpcs_by_method())["UnaryCall"]
      .mutable_rpcs_by_peer()
      ->insert({{"peer1", 2}, {"peer2", 1}});
  EXPECT_EQ(expected.DebugString(),
            watcher.WaitForRpcStatsResponse(0).DebugString());
}

TEST(XdsStatsWatcherTest, WaitForRpcStatsResponseIgnoresMetadata) {
  XdsStatsWatcher watcher(0, 3, {});
  // RPC had metadata - but watcher should ignore it
  watcher.RpcCompleted(BuildCallResult(0), "peer1",
                       {{"K1", "v1"}, {"k2", "v2"}, {"k3", "v3"}},
                       {{"K1", "t1"}, {"k2", "t2"}});
  watcher.RpcCompleted(BuildCallResult(1), "peer1", {{"k1", "v4"}}, {});
  watcher.RpcCompleted(BuildCallResult(2), "peer2", {},
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
