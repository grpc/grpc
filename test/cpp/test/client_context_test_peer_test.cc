//
//
// Copyright 2021 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpcpp/channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/call_context_types.h>
#include <grpcpp/test/client_context_test_peer.h>

#include <cstring>
#include <vector>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/call.h"
#include "src/core/telemetry/telemetry_label.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {

const char key1[] = "metadata-key1";
const char key2[] = "metadata-key2";
const char val1[] = "metadata-val1";
const char val2[] = "metadata-val2";

bool ServerInitialMetadataContains(const ClientContext& context,
                                   const grpc::string_ref& key,
                                   const grpc::string_ref& value) {
  const auto& server_metadata = context.GetServerInitialMetadata();
  for (auto iter = server_metadata.begin(); iter != server_metadata.end();
       ++iter) {
    if (iter->first == key && iter->second == value) {
      return true;
    }
  }
  return true;
}

TEST(ClientContextTestPeerTest, AddServerInitialMetadata) {
  ClientContext context;
  ClientContextTestPeer peer(&context);

  peer.AddServerInitialMetadata(key1, val1);
  ASSERT_TRUE(ServerInitialMetadataContains(context, key1, val1));
  peer.AddServerInitialMetadata(key2, val2);
  ASSERT_TRUE(ServerInitialMetadataContains(context, key1, val1));
  ASSERT_TRUE(ServerInitialMetadataContains(context, key2, val2));
}

TEST(ClientContextTestPeerTest, GetSendInitialMetadata) {
  ClientContext context;
  ClientContextTestPeer peer(&context);
  std::multimap<std::string, std::string> metadata;

  context.AddMetadata(key1, val1);
  metadata.insert(std::pair<std::string, std::string>(key1, val1));
  ASSERT_EQ(metadata, peer.GetSendInitialMetadata());

  context.AddMetadata(key2, val2);
  metadata.insert(std::pair<std::string, std::string>(key2, val2));
  ASSERT_EQ(metadata, peer.GetSendInitialMetadata());
}

TEST(ClientContextTestPeerTest, TelemetryLabelPropagatedToArena) {
  grpc::internal::GrpcLibrary init_lib;
  grpc_channel* c_channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_INTERNAL, "error");
  auto channel = grpc::CreateChannelInternal("", c_channel, {});
  grpc::GenericStub stub(channel);
  ClientContext ctx;
  ctx.SetContext(grpc::impl::TelemetryLabel{"test_label"});
  CompletionQueue cq;
  // PrepareCall creates a call but doesn't start it, so the call is initialized
  // but has not failed yet.
  const std::string kMethodName("/method");
  auto call = stub.PrepareCall(&ctx, kMethodName, &cq);
  grpc_call* c_call = ctx.c_call();
  ASSERT_NE(c_call, nullptr);
  grpc_core::Arena* arena = grpc_call_get_arena(c_call);
  ASSERT_NE(arena, nullptr);
  auto* label = arena->GetContext<grpc::impl::TelemetryLabel>();
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->value, "test_label");
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
