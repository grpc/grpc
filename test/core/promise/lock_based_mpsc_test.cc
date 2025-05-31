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

#include "src/core/lib/promise/lock_based_mpsc.h"

#include <grpc/support/log.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "test/core/promise/poll_matcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "fuzztest/fuzztest.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_core {
namespace {

template <typename T>
inline bool operator==(const MpscQueued<T>& a,
                       const std::pair<T, uint32_t>& b) {
  return *a == b.first && a.tokens() == b.second;
}

template <typename T>
inline bool operator!=(const MpscQueued<T>& a,
                       const std::pair<T, uint32_t>& b) {
  return !operator==(a, b);
}

template <typename T>
inline bool operator==(const ValueOrFailure<MpscQueued<T>>& a,
                       const std::pair<T, uint32_t>& b) {
  if (!a.ok()) return false;
  return *a == b;
}

template <typename T>
inline bool operator!=(const ValueOrFailure<MpscQueued<T>>& a,
                       const std::pair<T, uint32_t>& b) {
  return !operator==(a, b);
}

template <typename T>
inline bool operator==(const ValueOrFailure<MpscQueued<T>>& a, Failure) {
  return !a.ok();
}

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
  friend std::ostream& operator<<(std::ostream& os,
                                  const std::vector<Payload>& payloads) {
    os << "[";
    for (const auto& payload : payloads) {
      os << payload;
    }
    os << "]";
    return os;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Payload& payload) {
    if (payload.x == nullptr) {
      sink.Append("Payload{nullptr}");
    } else {
      sink.Append(absl::StrCat(*payload.x));
    }
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const std::vector<Payload>& payloads) {
    sink.Append("[");
    for (const auto& payload : payloads) {
      if (payload.x == nullptr) {
        sink.Append("Payload{nullptr}");
      } else {
        sink.Append(absl::StrCat(*payload.x));
      }
    }
    sink.Append("]");
  }
};
Payload MakePayload(int value) { return Payload{std::make_unique<int>(value)}; }

TEST(MpscTest, NoOp) { LockBasedMpscReceiver<Payload> receiver(1); }

TEST(MpscTest, MakeSender) {
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
}

TEST(MpscTest, SendOneThingInstantly) {
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(Success{}));
}

TEST(MpscTest, SendAckedOneThingWaitsForRead) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
  auto send = sender.SendAcked(MakePayload(1));
  EXPECT_THAT(send(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady());
  EXPECT_THAT(send(), IsReady(Success{}));
  activity.Deactivate();
}

TEST(MpscTest, SendOneThingInstantlyAndReceiveInstantly) {
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1))(), IsReady(Success{}));
  EXPECT_THAT(receiver.Next()(), IsReady(MakePayload(1)));
}

TEST(MpscTest, SendingLotsOfThingsGivesPushback) {
  StrictMock<MockActivity> activity1;
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
  EXPECT_THAT(sender.Send(MakePayload(2), 1)(), IsPending());
  activity1.Deactivate();
}

TEST(MpscTest, ReceivingAfterBlockageWakesUp) {
  StrictMock<MockActivity> activity1;
  StrictMock<MockActivity> activity2;
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
  auto send2 = sender.Send(MakePayload(2), 1);
  EXPECT_THAT(send2(), IsPending());
  activity1.Deactivate();

  activity2.Activate();
  EXPECT_CALL(activity1, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(1), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(2), 1u)));
  Mock::VerifyAndClearExpectations(&activity1);
  activity2.Deactivate();

  activity1.Activate();
  EXPECT_THAT(send2(), IsReady(Success{}));
  Mock::VerifyAndClearExpectations(&activity2);
  activity1.Deactivate();
}

TEST(MpscTest, BigBufferAllowsBurst) {
  LockBasedMpscReceiver<Payload> receiver(50);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(sender.Send(MakePayload(i), 1)(), IsReady(Success{}));
  }
  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(i), 1u)));
  }
  activity.Deactivate();
}

TEST(MpscTest, ClosureIsVisibleToSenders) {
  auto receiver = std::make_unique<LockBasedMpscReceiver<Payload>>(1);
  LockBasedMpscSender<Payload> sender = receiver->MakeSender();
  receiver.reset();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Failure{}));
}

TEST(MpscTest, ImmediateSendWorks) {
  StrictMock<MockActivity> activity;
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(6), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(7), 1), Success{});

  activity.Activate();
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(1), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(2), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(3), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(4), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(5), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(6), 1u)));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(7), 1u)));
  auto receive2 = receiver.Next();
  EXPECT_THAT(receive2(), IsPending());
  activity.Deactivate();
}

TEST(MpscTest, NextBatchWorks) {
  StrictMock<MockActivity> activity;
  MpscReceiver<Payload> receiver(10);
  MpscSender<Payload> sender = receiver.MakeSender();

  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(6), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(7), 1), Success{});

  activity.Activate();
  {
    auto r = receiver.NextBatch(std::numeric_limits<size_t>::max())();
    ASSERT_TRUE(r.ready());
    ASSERT_TRUE(r.value().ok());
    ASSERT_EQ(r.value()->size(), 7u);
    EXPECT_EQ((*r.value())[0], MakePayload(1));
    EXPECT_EQ((*r.value())[1], MakePayload(2));
    EXPECT_EQ((*r.value())[2], MakePayload(3));
    EXPECT_EQ((*r.value())[3], MakePayload(4));
    EXPECT_EQ((*r.value())[4], MakePayload(5));
    EXPECT_EQ((*r.value())[5], MakePayload(6));
    EXPECT_EQ((*r.value())[6], MakePayload(7));
    auto receive2 = receiver.NextBatch(std::numeric_limits<size_t>::max());
    EXPECT_THAT(receive2(), IsPending());
  }
  activity.Deactivate();
}

TEST(MpscTest, NextBatchRespectsMaxBatchSize) {
  StrictMock<MockActivity> activity;
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(6), 1), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(7), 1), Success{});

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
  LockBasedMpscReceiver<Payload> receiver(1);
  activity.Activate();
  auto next = receiver.Next();
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  receiver.MarkClosed();
  EXPECT_THAT(next(), IsReady(Failure{}));
  activity.Deactivate();
}

TEST(MpscTest, BigBufferBulkReceive) {
  LockBasedMpscReceiver<Payload> receiver(50);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();

  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(sender.Send(MakePayload(i))(), IsReady(Success{}));
  }
  auto result = receiver.NextBatch()();
  std::vector<Payload> expected;
  for (int i = 0; i < 25; i++) {
    expected.emplace_back(MakePayload(i));
  }
  EXPECT_THAT(result, IsReady(expected));
}

TEST(MpscTest, BulkReceive) {
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5)), Success{});
  auto promise = receiver.NextBatch();
  auto result = promise();

  std::vector<Payload> expected;
  expected.emplace_back(MakePayload(1));
  expected.emplace_back(MakePayload(2));
  expected.emplace_back(MakePayload(3));
  expected.emplace_back(MakePayload(4));
  expected.emplace_back(MakePayload(5));
  EXPECT_THAT(result, IsReady(expected));
}

TEST(MpscTest, BulkAndSingleReceive) {
  LockBasedMpscReceiver<Payload> receiver(1);
  LockBasedMpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(1)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(2)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(3)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(4)), Success{});
  EXPECT_EQ(sender.UnbufferedImmediateSend(MakePayload(5)), Success{});
  auto promise = receiver.Next();
  auto result = promise();
  EXPECT_THAT(result, IsReady(MakePayload(1)));

  auto bulk_promise = receiver.NextBatch();
  auto bulk_result = bulk_promise();
  std::vector<Payload> expected;
  expected.emplace_back(MakePayload(2));
  expected.emplace_back(MakePayload(3));
  expected.emplace_back(MakePayload(4));
  expected.emplace_back(MakePayload(5));
  EXPECT_THAT(bulk_result, IsReady(expected));
}

TEST(MpscTest, BulkReceiveAfterClose) {
  LockBasedMpscReceiver<Payload> receiver(1);
  receiver.MarkClosed();
  auto promise = receiver.NextBatch();
  auto result = promise();
  EXPECT_THAT(result, IsReady(Failure{}));
}

TEST(MpscTest, CloseAfterBulkReceive) {
  StrictMock<MockActivity> activity;
  LockBasedMpscReceiver<Payload> receiver(1);
  activity.Activate();
  auto next = receiver.NextBatch();
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  receiver.MarkClosed();
  EXPECT_THAT(next(), IsReady(Failure{}));
  activity.Deactivate();
}

TEST(MpscTest, ManySendsBulkReceive) {
  StrictMock<MockActivity> activity;
  LockBasedMpscReceiver<Payload> receiver(10);

  auto multi_send = [i = 0, max = 100,
                     sender =
                         receiver.MakeSender()]() mutable -> Poll<StatusFlag> {
    if (i >= max) {
      return Success{};
    }
    int cur_limit = std::min(i + 10, max);
    while (i < cur_limit) {
      sender.Send(MakePayload(i))();
      i++;
    }
    return Pending{};
  };
  activity.Activate();
  multi_send();
  activity.Deactivate();

  for (int i = 0; i < 10; i++) {
    EXPECT_CALL(activity, WakeupRequested());
    auto promise = receiver.NextBatch();
    auto result = promise();
    std::vector<Payload> expected;
    int start = i * 10;
    for (int j = start; j < start + 10; j++) {
      expected.emplace_back(MakePayload(j));
    }
    EXPECT_THAT(result, IsReady(expected));
    Mock::VerifyAndClearExpectations(&activity);

    activity.Activate();
    if (i < 9) {
      EXPECT_THAT(multi_send(), IsPending());
    }
    activity.Deactivate();
  }

  EXPECT_THAT(multi_send(), IsReady(Success{}));
  activity.Activate();
  EXPECT_THAT(receiver.NextBatch()(), IsPending());
  activity.Deactivate();
}

}  // namespace
}  // namespace grpc_core
