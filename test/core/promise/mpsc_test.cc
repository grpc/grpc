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

#include <memory>
#include <string>

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/promise.h"

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
};
Payload MakePayload(int value) { return {std::make_unique<int>(value)}; }

TEST(MpscTest, NoOp) { MpscReceiver<Payload> receiver(1); }

TEST(MpscTest, MakeSender) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
}

TEST(MpscTest, SendOneThingInstantly) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(1))), true);
}

TEST(MpscTest, SendOneThingInstantlyAndReceiveInstantly) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(1))), true);
  EXPECT_EQ(NowOrNever(receiver.Next()), MakePayload(1));
}

TEST(MpscTest, SendingLotsOfThingsGivesPushback) {
  StrictMock<MockActivity> activity1;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(1))), true);
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(2))), absl::nullopt);
  activity1.Deactivate();
}

TEST(MpscTest, ReceivingAfterBlockageWakesUp) {
  StrictMock<MockActivity> activity1;
  StrictMock<MockActivity> activity2;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(1))), true);
  auto send2 = sender.Send(MakePayload(2));
  EXPECT_EQ(send2(), Poll<bool>(Pending{}));
  activity1.Deactivate();

  activity2.Activate();
  EXPECT_CALL(activity1, WakeupRequested());
  EXPECT_EQ(NowOrNever(receiver.Next()), MakePayload(1));
  Mock::VerifyAndClearExpectations(&activity1);
  auto receive2 = receiver.Next();
  EXPECT_EQ(receive2(), Poll<Payload>(Pending{}));
  activity2.Deactivate();

  activity1.Activate();
  EXPECT_CALL(activity2, WakeupRequested());
  EXPECT_EQ(send2(), Poll<bool>(true));
  Mock::VerifyAndClearExpectations(&activity2);
  activity1.Deactivate();

  activity2.Activate();
  EXPECT_EQ(receive2(), Poll<Payload>(MakePayload(2)));
  activity2.Deactivate();
}

TEST(MpscTest, BigBufferAllowsBurst) {
  MpscReceiver<Payload> receiver(50);
  MpscSender<Payload> sender = receiver.MakeSender();

  for (int i = 0; i < 25; i++) {
    EXPECT_EQ(NowOrNever(sender.Send(MakePayload(i))), true);
  }
  for (int i = 0; i < 25; i++) {
    EXPECT_EQ(NowOrNever(receiver.Next()), MakePayload(i));
  }
}

TEST(MpscTest, ClosureIsVisibleToSenders) {
  auto receiver = std::make_unique<MpscReceiver<Payload>>(1);
  MpscSender<Payload> sender = receiver->MakeSender();
  receiver.reset();
  EXPECT_EQ(NowOrNever(sender.Send(MakePayload(1))), false);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
