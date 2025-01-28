// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/mpsc.h"

#include <grpc/support/log.h>

#include <memory>
#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/promise.h"
#include "test/core/promise/poll_matcher.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_core {
namespace {

class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask) override { WakeupRequested(); }
  void Drop(WakeupMask) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask) const override { return DebugTag(); }

  void Activate() {
    if (scoped_activity_ != nullptr) return;
    scoped_activity_ = std::make_unique<ScopedActivity>(this);
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

struct Payload {
  std::unique_ptr<int> x;
  bool operator==(const Payload& other) const {
    return (x == nullptr && other.x == nullptr) ||
           (x != nullptr && other.x != nullptr && *x == *other.x);
  }
  bool operator!=(const Payload& other) const { return !(*this == other); }
  explicit Payload(std::unique_ptr<int> x) : x(std::move(x)) {}
  Payload(const Payload& other)
      : x(other.x ? std::make_unique<int>(*other.x) : nullptr) {}

  friend std::ostream& operator<<(std::ostream& os, const Payload& payload) {
    if (payload.x == nullptr) return os << "Payload{nullptr}";
    return os << "Payload{" << *payload.x << "}";
  }
};
Payload MakePayload(int value) { return Payload{std::make_unique<int>(value)}; }

TEST(MpscTest, NoOp) { MpscReceiver<Payload> receiver(1); }

TEST(MpscTest, MakeSender) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
}

TEST(MpscTest, SendOneThingInstantly) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(true));
}

TEST(MpscTest, SendAckedOneThingWaitsForRead) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  auto send = sender.SendAcked(MakePayload(1));
  EXPECT_THAT(send(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady());
  EXPECT_THAT(send(), IsReady(true));
  activity.Deactivate();
}

TEST(MpscTest, SendOneThingInstantlyAndReceiveInstantly) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(true));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(1)));
}

TEST(MpscTest, SendingLotsOfThingsGivesPushback) {
  StrictMock<MockActivity> activity1;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(true));
  EXPECT_THAT(sender.Send(MakePayload(2))(), IsPending());
  activity1.Deactivate();

  EXPECT_CALL(activity1, WakeupRequested());
}

TEST(MpscTest, ReceivingAfterBlockageWakesUp) {
  StrictMock<MockActivity> activity1;
  StrictMock<MockActivity> activity2;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(true));
  auto send2 = sender.Send(MakePayload(2));
  EXPECT_THAT(send2(), IsPending());
  activity1.Deactivate();

  activity2.Activate();
  EXPECT_CALL(activity1, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(1)));
  Mock::VerifyAndClearExpectations(&activity1);
  auto receive2 = receiver.Next();
  EXPECT_THAT(receive2(), IsReady(MakePayload(2)));
  activity2.Deactivate();

  activity1.Activate();
  EXPECT_THAT(send2(), Poll<bool>(true));
  Mock::VerifyAndClearExpectations(&activity2);
  activity1.Deactivate();
}

TEST(MpscTest, BigBufferAllowsBurst) {
  MpscReceiver<Payload> receiver(50);
  MpscSender<Payload> sender = receiver.MakeSender();

  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(sender.Send(MakePayload(i))(), IsReady(true));
  }
  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(i)));
  }
}

TEST(MpscTest, ClosureIsVisibleToSenders) {
  auto receiver = std::make_unique<MpscReceiver<Payload>>(1);
  MpscSender<Payload> sender = receiver->MakeSender();
  receiver.reset();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(false));
}

TEST(MpscTest, ImmediateSendWorks) {
  StrictMock<MockActivity> activity;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(6)), true);
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(7)), true);

  activity.Activate();
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(1)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(2)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(3)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(4)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(5)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(6)));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(7)));
  auto receive2 = receiver.Next();
  EXPECT_THAT(receive2(), IsPending());
  activity.Deactivate();
}

TEST(MpscTest, CloseFailsNext) {
  StrictMock<MockActivity> activity;
  MpscReceiver<Payload> receiver(1);
  activity.Activate();
  auto next = receiver.Next();
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  receiver.MarkClosed();
  EXPECT_THAT(next(), IsReady(Failure{}));
  activity.Deactivate();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
