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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "test/core/promise/poll_matcher.h"

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

TEST(MpscTest, NoOp) { MpscReceiver<Payload> receiver(1); }

TEST(MpscTest, MakeSender) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
}

TEST(MpscTest, SendOneThingInstantly) {
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
}

TEST(MpscTest, SendAckedOneThingWaitsForRead) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  auto send = sender.Send(MakePayload(1), 2);
  EXPECT_THAT(send(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady());
  EXPECT_THAT(send(), IsReady(Success{}));
  activity.Deactivate();
}

TEST(MpscTest, SendOneThingInstantlyAndReceiveInstantly) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(1), 1u)));
  activity.Deactivate();
}

TEST(MpscTest, SendingLotsOfThingsGivesPushback) {
  StrictMock<MockActivity> activity1;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
  EXPECT_THAT(sender.Send(MakePayload(2), 1)(), IsPending());
  activity1.Deactivate();

  EXPECT_CALL(activity1, WakeupRequested());  // For the pending send.
}

TEST(MpscTest, ReceivingAfterBlockageWakesUp) {
  StrictMock<MockActivity> activity1;
  StrictMock<MockActivity> activity2;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

  activity1.Activate();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Success{}));
  auto send2 = sender.Send(MakePayload(2), 1);
  EXPECT_THAT(send2(), IsPending());
  activity1.Deactivate();

  activity2.Activate();
  EXPECT_CALL(activity1, WakeupRequested());
  EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(1), 1u)));
  Mock::VerifyAndClearExpectations(&activity1);
  auto receive2 = receiver.Next();
  EXPECT_THAT(receive2(), IsReady(std::pair(MakePayload(2), 1u)));
  activity2.Deactivate();

  activity1.Activate();
  EXPECT_THAT(send2(), IsReady(Success{}));
  Mock::VerifyAndClearExpectations(&activity2);
  activity1.Deactivate();
}

TEST(MpscTest, BigBufferAllowsBurst) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  MpscReceiver<Payload> receiver(50);
  MpscSender<Payload> sender = receiver.MakeSender();

  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(sender.Send(MakePayload(i), 1)(), IsReady(Success{}));
  }
  for (int i = 0; i < 25; i++) {
    EXPECT_THAT(receiver.Next()(), IsReady(std::pair(MakePayload(i), 1u)));
  }
  activity.Deactivate();
}

TEST(MpscTest, ClosureIsVisibleToSenders) {
  auto receiver = std::make_unique<MpscReceiver<Payload>>(1);
  MpscSender<Payload> sender = receiver->MakeSender();
  receiver.reset();
  EXPECT_THAT(sender.Send(MakePayload(1), 1)(), IsReady(Failure{}));
}

TEST(MpscTest, ImmediateSendWorks) {
  StrictMock<MockActivity> activity;
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();

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

TEST(MpscTest, AllNextWorks) {
  StrictMock<MockActivity> activity;
  MpscReceiver<Payload> receiver(1);
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
    auto r = receiver.AllNext()();
    ASSERT_TRUE(r.ready());
    ASSERT_TRUE(r.value().ok());
    ASSERT_EQ(r.value()->tokens(), 7u);
    ASSERT_EQ((*r.value())->size(), 7u);
    EXPECT_EQ((**r.value())[0], MakePayload(1));
    EXPECT_EQ((**r.value())[1], MakePayload(2));
    EXPECT_EQ((**r.value())[2], MakePayload(3));
    EXPECT_EQ((**r.value())[3], MakePayload(4));
    EXPECT_EQ((**r.value())[4], MakePayload(5));
    EXPECT_EQ((**r.value())[5], MakePayload(6));
    EXPECT_EQ((**r.value())[6], MakePayload(7));
    auto receive2 = receiver.AllNext();
    EXPECT_THAT(receive2(), IsPending());
  }
  activity.Deactivate();
}

void SendsConsistent(uint64_t max_queued, std::vector<uint32_t> poll_order,
                     absl::flat_hash_map<uint32_t, uint32_t> weights) {
  StrictMock<MockActivity> activity;
  EXPECT_CALL(activity, WakeupRequested()).Times(::testing::AnyNumber());
  std::map<uint32_t, absl::AnyInvocable<bool()>> sends;
  std::vector<uint32_t> send_order;
  std::vector<uint32_t> receive_order;
  absl::flat_hash_set<uint32_t> done_set;
  MpscReceiver<Payload> receiver(max_queued);
  MpscSender<Payload> sender = receiver.MakeSender();
  auto receive_once = [fn = receiver.Next(), &receiver,
                       &receive_order]() mutable {
    auto r = fn();
    if (r.pending()) return;
    CHECK(r.value().ok());
    receive_order.push_back(*(**r.value()).x);
  };
  for (auto x : poll_order) {
    activity.Activate();
    if (x == 0) {
      receive_once();
    } else {
      if (auto it = sends.find(x); it != sends.end()) {
        if (it->second()) {
          sends.erase(it);
          done_set.insert(x);
        }
      } else if (done_set.count(x) == 0) {
        send_order.push_back(x);
        auto it = weights.find(x);
        uint32_t weight = it == weights.end() ? 1 : it->second;
        sends.emplace(x,
                      [send = sender.Send(MakePayload(x), weight)]() mutable {
                        auto r = send();
                        if (r.pending()) return false;
                        EXPECT_TRUE(r.value().ok());
                        return true;
                      });
      }
    }
    activity.Deactivate();
  }
  while (receive_order.size() < send_order.size()) {
    activity.Activate();
    receive_once();
    activity.Deactivate();
  }
  EXPECT_EQ(send_order, receive_order);
}
FUZZ_TEST(MpscTest, SendsConsistent);

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
