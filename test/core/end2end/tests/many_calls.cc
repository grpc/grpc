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

#include <grpc/status.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTests, ManyCallsOrder1) {
  if (test_config()->feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) {
    GTEST_SKIP() << "Skipped for proxies at the moment";
  }
  static const uint64_t kNumCalls = 100;
  std::vector<Call> client_calls;
  std::vector<IncomingMetadata> server_initial_metadata(kNumCalls);
  std::vector<IncomingStatusOnClient> server_status(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    client_calls.emplace_back(
        NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create());
    client_calls.back()
        .NewBatch(i)
        .SendInitialMetadata({})
        .SendMessage("hello world")
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata[i])
        .RecvStatusOnClient(server_status[i]);
  }
  std::vector<IncomingCall> server_calls;
  std::vector<IncomingMessage> client_messages(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    server_calls.emplace_back(RequestCall(1000 + i));
    Expect(1000 + i, true);
    Step();
    server_calls.back().NewBatch(2000 + i).SendInitialMetadata({}).RecvMessage(
        client_messages[i]);
    Expect(2000 + i, true);
    Step();
  }
  std::vector<IncomingCloseOnServer> client_close(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    server_calls[i]
        .NewBatch(3000 + i)
        .RecvCloseOnServer(client_close[i])
        .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  }
  for (int i = 0; i < kNumCalls; ++i) {
    Expect(3000 + i, true);
    Expect(i, true);
  }
  Step();
  for (int i = 0; i < kNumCalls; ++i) {
    EXPECT_EQ(server_status[i].status(), GRPC_STATUS_OK);
    EXPECT_EQ(server_status[i].message(), IsErrorFlattenEnabled() ? "" : "xyz");
    EXPECT_EQ(server_calls[i].method(), "/foo");
    EXPECT_FALSE(client_close[i].was_cancelled());
    EXPECT_EQ(client_messages[i].payload(), "hello world");
  }
}

CORE_END2END_TEST(CoreEnd2endTests, ManyCallsOrder2) {
  if (test_config()->feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) {
    GTEST_SKIP() << "Skipped for proxies at the moment";
  }
  static const uint64_t kNumCalls = 100;
  std::vector<Call> client_calls;
  std::vector<IncomingMetadata> server_initial_metadata(kNumCalls);
  std::vector<IncomingStatusOnClient> server_status(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    client_calls.emplace_back(
        NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create());
    client_calls.back()
        .NewBatch(i)
        .SendInitialMetadata({})
        .SendMessage("hello world")
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata[i])
        .RecvStatusOnClient(server_status[i]);
  }
  std::vector<IncomingCall> server_calls;
  std::vector<IncomingMessage> client_messages(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    server_calls.emplace_back(RequestCall(1000 + i));
    Expect(1000 + i, true);
  }
  Step();
  for (int i = 0; i < kNumCalls; ++i) {
    server_calls[i].NewBatch(2000 + i).SendInitialMetadata({}).RecvMessage(
        client_messages[i]);
    Expect(2000 + i, true);
  }
  Step();
  std::vector<IncomingCloseOnServer> client_close(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    server_calls[i]
        .NewBatch(3000 + i)
        .RecvCloseOnServer(client_close[i])
        .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  }
  for (int i = 0; i < kNumCalls; ++i) {
    Expect(3000 + i, true);
    Expect(i, true);
  }
  Step();
  for (int i = 0; i < kNumCalls; ++i) {
    EXPECT_EQ(server_status[i].status(), GRPC_STATUS_OK);
    EXPECT_EQ(server_status[i].message(), IsErrorFlattenEnabled() ? "" : "xyz");
    EXPECT_EQ(server_calls[i].method(), "/foo");
    EXPECT_FALSE(client_close[i].was_cancelled());
    EXPECT_EQ(client_messages[i].payload(), "hello world");
  }
}

}  // namespace
}  // namespace grpc_core
