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

#include <stdio.h>
#include <string.h>

#include <functional>
#include <memory>

#include "gmock/gmock.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

// Request with a large amount of metadata.
TEST_P(Http2SingleHopTest, RequestWithLargeMetadata) {
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, 64 * 1024 + 1024));
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, 64 * 1024 + 1024));
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({
          {"key", std::string(64 * 1024, 'a')},
      })
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Server: send initial metadata and receive request.
  CoreEnd2endTest::IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  Expect(102, true);
  Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  Expect(103, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "hello world");
  EXPECT_EQ(s.GetInitialMetadata("key"), std::string(64 * 1024, 'a'));
}

// Server responds with metadata larger than what the client accepts.
TEST_P(Http2SingleHopTest, RequestWithBadLargeMetadataResponse) {
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, 1024));
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_METADATA_SIZE, 1024));
  for (int i = 0; i < 10; i++) {
    auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
    // Client: send request.
    CoreEnd2endTest::IncomingStatusOnClient server_status;
    CoreEnd2endTest::IncomingMetadata server_initial_metadata;
    c.NewBatch(1)
        .SendInitialMetadata({})
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    auto s = RequestCall(101);
    Expect(101, true);
    Step();
    CoreEnd2endTest::IncomingCloseOnServer client_close;
    s.NewBatch(102)
        .SendInitialMetadata({
            {"key", std::string(64 * 1024, 'a')},
        })
        .RecvCloseOnServer(client_close)
        .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
    Expect(102, true);
    Expect(1, true);
    Step();
    EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
    EXPECT_THAT(
        server_status.message(),
        ::testing::StartsWith("received initial metadata size exceeds limit"));
    EXPECT_EQ(s.method(), "/foo");
  }
}

}  // namespace
}  // namespace grpc_core
