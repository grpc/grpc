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

#include <string.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreDeadlineTest, TimeoutBeforeRequestCall) {
  SKIP_IF_CHAOTIC_GOOD();
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(1)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  auto s = RequestCall(2);
  bool got_call = false;
  std::unique_ptr<IncomingCloseOnServer> client_close;
  Expect(2, MaybePerformAction{[this, &s, &got_call, &client_close](bool ok) {
           got_call = true;
           if (ok) {
             // If we successfully get a call, then we should additionally get a
             // close tag.
             client_close = std::make_unique<IncomingCloseOnServer>();
             s.NewBatch(3).RecvCloseOnServer(*client_close);
             Expect(3, true);
           }
         }});
  Step();
  if (client_close != nullptr) {
    // If we got a close op then it should indicate cancelled.
    EXPECT_TRUE(got_call);
    EXPECT_TRUE(client_close->was_cancelled());
  }
  if (!got_call) {
    // Maybe we didn't get a call (didn't reach the server pre-deadline).
    // In that case we should get a failed call back on shutdown.
    ShutdownServerAndNotify(4);
    Expect(2, false);
    Expect(4, true);
    Step();
  }
}

CORE_END2END_TEST(CoreDeadlineTest,
                  TimeoutBeforeRequestCallWithRegisteredMethod) {
  SKIP_IF_CHAOTIC_GOOD();
  auto method = RegisterServerMethod("/foo", GRPC_SRM_PAYLOAD_NONE);

  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(1)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  auto s = RequestRegisteredCall(method, 2);
  bool got_call = false;
  std::unique_ptr<IncomingCloseOnServer> client_close;
  Expect(2, MaybePerformAction{[this, &s, &got_call, &client_close](bool ok) {
           got_call = true;
           if (ok) {
             // If we successfully get a call, then we should additionally get a
             // close tag.
             client_close = std::make_unique<IncomingCloseOnServer>();
             s.NewBatch(3).RecvCloseOnServer(*client_close);
             Expect(3, true);
           }
         }});
  Step();
  if (client_close != nullptr) {
    // If we got a close op then it should indicate cancelled.
    EXPECT_TRUE(got_call);
    EXPECT_TRUE(client_close->was_cancelled());
  }
  if (!got_call) {
    // Maybe we didn't get a call (didn't reach the server pre-deadline).
    // In that case we should get a failed call back on shutdown.
    ShutdownServerAndNotify(4);
    Expect(2, false);
    Expect(4, true);
    Step();
  }
}

CORE_END2END_TEST(CoreDeadlineSingleHopTest,
                  TimeoutBeforeRequestCallWithRegisteredMethodWithPayload) {
  SKIP_IF_CHAOTIC_GOOD();
  auto method =
      RegisterServerMethod("/foo", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER);

  const size_t kMessageSize = 10 * 1024 * 1024;
  auto send_from_client = RandomSlice(kMessageSize);
  InitServer(
      ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, kMessageSize));
  InitClient(
      ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, kMessageSize));

  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(1)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .SendMessage(send_from_client.Ref())
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  IncomingMessage client_message;
  auto s = RequestRegisteredCall(method, &client_message, 2);
  bool got_call = false;
  std::unique_ptr<IncomingCloseOnServer> client_close;
  Expect(2, MaybePerformAction{[this, &s, &got_call, &client_close](bool ok) {
           gpr_log(GPR_INFO, "\n***\n*** got call: %d\n***", ok);
           got_call = true;
           if (ok) {
             // If we successfully get a call, then we should additionally get a
             // close tag.
             client_close = std::make_unique<IncomingCloseOnServer>();
             s.NewBatch(3).RecvCloseOnServer(*client_close);
             Expect(3, true);
           }
         }});
  Step();
  if (client_close != nullptr) {
    // If we got a close op then it should indicate cancelled.
    EXPECT_TRUE(got_call);
    EXPECT_TRUE(client_close->was_cancelled());
  }
  if (!got_call) {
    // Maybe we didn't get a call (didn't reach the server pre-deadline).
    // In that case we should get a failed call back on shutdown.
    ShutdownServerAndNotify(4);
    Expect(2, false);
    Expect(4, true);
    Step();
  }
}

}  // namespace
}  // namespace grpc_core
