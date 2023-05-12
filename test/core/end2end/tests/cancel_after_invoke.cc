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

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"

namespace grpc_core {

void CancelAfterInvoke6(CoreEnd2endTest& test,
                        std::unique_ptr<CancellationMode> mode) {
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient()
      .RecvMessage(server_message);
  mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(mode->ExpectedStatus(), GRPC_STATUS_INTERNAL));
}

void CancelAfterInvoke5(CoreEnd2endTest& test,
                        std::unique_ptr<CancellationMode> mode) {
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient();
  mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(mode->ExpectedStatus(), GRPC_STATUS_INTERNAL));
}

void CancelAfterInvoke4(CoreEnd2endTest& test,
                        std::unique_ptr<CancellationMode> mode) {
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024));
  mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(mode->ExpectedStatus(), GRPC_STATUS_INTERNAL));
}

void CancelAfterInvoke3(CoreEnd2endTest& test,
                        std::unique_ptr<CancellationMode> mode) {
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({});
  mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(mode->ExpectedStatus(), GRPC_STATUS_INTERNAL));
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterInvoke6) {
  CancelAfterInvoke6(*this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterInvoke5) {
  CancelAfterInvoke5(*this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterInvoke4) {
  CancelAfterInvoke4(*this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterInvoke3) {
  CancelAfterInvoke3(*this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterInvoke6) {
  CancelAfterInvoke6(*this, std::make_unique<DeadlineCancellationMode>());
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterInvoke5) {
  CancelAfterInvoke5(*this, std::make_unique<DeadlineCancellationMode>());
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterInvoke4) {
  CancelAfterInvoke4(*this, std::make_unique<DeadlineCancellationMode>());
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterInvoke3) {
  CancelAfterInvoke3(*this, std::make_unique<DeadlineCancellationMode>());
}

}  // namespace grpc_core
