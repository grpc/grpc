// Copyright 2024 gRPC authors.
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

#include "src/core/call/request_buffer.h"

#include "gtest/gtest.h"
#include "test/core/promise/poll_matcher.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_core {

namespace {
void CrashOnParseError(absl::string_view error, const Slice& data) {
  LOG(FATAL) << "Failed to parse " << error << " from "
             << data.as_string_view();
}

// A mock activity that can be activated and deactivated.
class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask /*mask*/) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Drop(WakeupMask /*mask*/) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask /*mask*/) const override {
    return DebugTag();
  }

  void Activate() {
    if (scoped_activity_ == nullptr) {
      scoped_activity_ = std::make_unique<ScopedActivity>(this);
    }
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

#define EXPECT_WAKEUP(activity, statement)                                 \
  EXPECT_CALL((activity), WakeupRequested()).Times(::testing::AtLeast(1)); \
  statement;                                                               \
  Mock::VerifyAndClearExpectations(&(activity));

ClientMetadataHandle TestMetadata() {
  ClientMetadataHandle md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Append("key", Slice::FromStaticString("value"), CrashOnParseError);
  return md;
}

MessageHandle TestMessage(int index = 0) {
  return Arena::MakePooled<Message>(
      SliceBuffer(Slice::FromCopiedString(absl::StrCat("message ", index))), 0);
}

MATCHER(IsTestMetadata, "") {
  if (arg == nullptr) return false;
  std::string backing;
  if (arg->GetStringValue("key", &backing) != "value") {
    *result_listener << arg->DebugString();
    return false;
  }
  return true;
}

MATCHER(IsTestMessage, "") {
  if (arg == nullptr) return false;
  if (arg->flags() != 0) {
    *result_listener << "flags: " << arg->flags();
    return false;
  }
  if (arg->payload()->JoinIntoString() != "message 0") {
    *result_listener << "payload: " << arg->payload()->JoinIntoString();
    return false;
  }
  return true;
}

MATCHER_P(IsTestMessage, index, "") {
  if (arg == nullptr) return false;
  if (arg->flags() != 0) {
    *result_listener << "flags: " << arg->flags();
    return false;
  }
  if (arg->payload()->JoinIntoString() != absl::StrCat("message ", index)) {
    *result_listener << "payload: " << arg->payload()->JoinIntoString();
    return false;
  }
  return true;
}

}  // namespace

TEST(RequestBufferTest, NoOp) { RequestBuffer buffer; }

TEST(RequestBufferTest, PushThenPullClientInitialMetadata) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto poll = reader.PullClientInitialMetadata()();
  ASSERT_THAT(poll, IsReady());
  auto value = std::move(poll.value());
  ASSERT_TRUE(value.ok());
  EXPECT_THAT(*value, IsTestMetadata());
}

TEST(RequestBufferTest, PushThenFinishThenPullClientInitialMetadata) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  buffer.FinishSends();
  RequestBuffer::Reader reader(&buffer);
  auto poll = reader.PullClientInitialMetadata()();
  ASSERT_THAT(poll, IsReady());
  auto value = std::move(poll.value());
  ASSERT_TRUE(value.ok());
  EXPECT_THAT(*value, IsTestMetadata());
}

TEST(RequestBufferTest, PullThenPushClientInitialMetadata) {
  StrictMock<MockActivity> activity;
  RequestBuffer buffer;
  RequestBuffer::Reader reader(&buffer);
  activity.Activate();
  auto poller = reader.PullClientInitialMetadata();
  auto poll = poller();
  EXPECT_THAT(poll, IsPending());
  ClientMetadataHandle md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Append("key", Slice::FromStaticString("value"), CrashOnParseError);
  EXPECT_WAKEUP(activity,
                EXPECT_EQ(buffer.PushClientInitialMetadata(std::move(md)), 40));
  poll = poller();
  ASSERT_THAT(poll, IsReady());
  auto value = std::move(poll.value());
  ASSERT_TRUE(value.ok());
  EXPECT_THAT(*value, IsTestMetadata());
}

TEST(RequestBufferTest, PushThenPullMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PushThenPullMessageStreamBeforeInitialMetadata) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  buffer.Commit(&reader);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PushThenPullMessageStreamBeforeFirstMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  buffer.Commit(&reader);
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullThenPushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  EXPECT_TRUE(poll_msg.pending());
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher(), IsReady(49)));
  poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullThenPushMessageSwitchBeforePullMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  buffer.Commit(&reader);
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  EXPECT_TRUE(poll_msg.pending());
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher(), IsReady(0)));
  poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullThenPushMessageSwitchBeforePushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  EXPECT_TRUE(poll_msg.pending());
  buffer.Commit(&reader);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher(), IsReady(0)));
  poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullThenPushMessageSwitchAfterPushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  EXPECT_TRUE(poll_msg.pending());
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher(), IsReady(49)));
  buffer.Commit(&reader);
  poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullEndOfStream) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, PullEndOfStreamSwitchBeforePullMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  buffer.Commit(&reader);
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, PullEndOfStreamSwitchBeforePushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  buffer.Commit(&reader);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsPending());
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_md(), IsReady()));  // value tested elsewhere
  EXPECT_THAT(pusher(), IsReady(0));
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, PullEndOfStreamQueuedWithMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest,
     PullEndOfStreamQueuedWithMessageSwitchBeforePushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  buffer.Commit(&reader);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsPending());
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_md(), IsReady()));  // value tested elsewhere
  EXPECT_THAT(pusher(), IsReady(0));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest,
     PullEndOfStreamQueuedWithMessageSwitchBeforePullMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  buffer.Commit(&reader);
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest,
     PullEndOfStreamQueuedWithMessageSwitchDuringPullMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  buffer.Commit(&reader);
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, PushThenPullMessageRepeatedly) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  for (int i = 0; i < 10; i++) {
    auto pusher = buffer.PushMessage(TestMessage(i));
    EXPECT_THAT(pusher(), IsReady(40 + 9 * (i + 1)));
    auto pull_msg = reader.PullMessage();
    auto poll_msg = pull_msg();
    ASSERT_TRUE(poll_msg.ready());
    ASSERT_TRUE(poll_msg.value().ok());
    ASSERT_TRUE(poll_msg.value().value().has_value());
    EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage(i));
  }
}

TEST(RequestBufferTest, PushSomeSwitchThenPushPullMessages) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  for (int i = 0; i < 10; i++) {
    auto pusher = buffer.PushMessage(TestMessage(i));
    EXPECT_THAT(pusher(), IsReady(40 + 9 * (i + 1)));
  }
  buffer.Commit(&reader);
  for (int i = 0; i < 10; i++) {
    auto pull_msg = reader.PullMessage();
    auto poll_msg = pull_msg();
    ASSERT_TRUE(poll_msg.ready());
    ASSERT_TRUE(poll_msg.value().ok());
    ASSERT_TRUE(poll_msg.value().value().has_value());
    EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage(i));
  }
  for (int i = 0; i < 10; i++) {
    auto pusher = buffer.PushMessage(TestMessage(i));
    EXPECT_THAT(pusher(), IsReady(0));
    auto pull_msg = reader.PullMessage();
    auto poll_msg = pull_msg();
    ASSERT_TRUE(poll_msg.ready());
    ASSERT_TRUE(poll_msg.value().ok());
    ASSERT_TRUE(poll_msg.value().value().has_value());
    EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage(i));
  }
}

TEST(RequestBufferTest, HedgeReadMetadata) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader1(&buffer);
  RequestBuffer::Reader reader2(&buffer);
  auto pull_md1 = reader1.PullClientInitialMetadata();
  auto pull_md2 = reader2.PullClientInitialMetadata();
  auto poll_md1 = pull_md1();
  auto poll_md2 = pull_md2();
  ASSERT_THAT(poll_md1, IsReady());
  ASSERT_THAT(poll_md2, IsReady());
  auto value1 = std::move(poll_md1.value());
  auto value2 = std::move(poll_md2.value());
  ASSERT_TRUE(value1.ok());
  ASSERT_TRUE(value2.ok());
  EXPECT_THAT(*value1, IsTestMetadata());
  EXPECT_THAT(*value2, IsTestMetadata());
}

TEST(RequestBufferTest, HedgeReadMetadataSwitchBeforeFirstRead) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader1(&buffer);
  buffer.Commit(&reader1);
  RequestBuffer::Reader reader2(&buffer);
  auto pull_md1 = reader1.PullClientInitialMetadata();
  auto pull_md2 = reader2.PullClientInitialMetadata();
  auto poll_md1 = pull_md1();
  auto poll_md2 = pull_md2();
  ASSERT_THAT(poll_md1, IsReady());
  ASSERT_THAT(poll_md2, IsReady());
  auto value1 = std::move(poll_md1.value());
  auto value2 = std::move(poll_md2.value());
  ASSERT_TRUE(value1.ok());
  EXPECT_FALSE(value2.ok());
  EXPECT_THAT(*value1, IsTestMetadata());
}

TEST(RequestBufferTest, HedgeReadMetadataLate) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader1(&buffer);
  auto pull_md1 = reader1.PullClientInitialMetadata();
  auto poll_md1 = pull_md1();
  ASSERT_THAT(poll_md1, IsReady());
  auto value1 = std::move(poll_md1.value());
  ASSERT_TRUE(value1.ok());
  EXPECT_THAT(*value1, IsTestMetadata());
  RequestBuffer::Reader reader2(&buffer);
  auto pull_md2 = reader2.PullClientInitialMetadata();
  auto poll_md2 = pull_md2();
  ASSERT_THAT(poll_md2, IsReady());
  auto value2 = std::move(poll_md2.value());
  ASSERT_TRUE(value2.ok());
  EXPECT_THAT(*value2, IsTestMetadata());
}

TEST(RequestBufferTest, HedgeReadMetadataLateSwitchAfterPullInitialMetadata) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader1(&buffer);
  auto pull_md1 = reader1.PullClientInitialMetadata();
  auto poll_md1 = pull_md1();
  ASSERT_THAT(poll_md1, IsReady());
  auto value1 = std::move(poll_md1.value());
  ASSERT_TRUE(value1.ok());
  EXPECT_THAT(*value1, IsTestMetadata());
  RequestBuffer::Reader reader2(&buffer);
  buffer.Commit(&reader1);
  auto pull_md2 = reader2.PullClientInitialMetadata();
  auto poll_md2 = pull_md2();
  ASSERT_THAT(poll_md2, IsReady());
  auto value2 = std::move(poll_md2.value());
  EXPECT_FALSE(value2.ok());
}

TEST(RequestBufferTest, StreamingPushBeforeLastMessagePulled) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  buffer.Commit(&reader);
  auto pusher1 = buffer.PushMessage(TestMessage(1));
  EXPECT_THAT(pusher1(), IsReady(0));
  auto pusher2 = buffer.PushMessage(TestMessage(2));
  EXPECT_THAT(pusher2(), IsPending());
  auto pull1 = reader.PullMessage();
  EXPECT_WAKEUP(activity, auto poll1 = pull1());
  ASSERT_TRUE(poll1.ready());
  ASSERT_TRUE(poll1.value().ok());
  ASSERT_TRUE(poll1.value().value().has_value());
  EXPECT_THAT(poll1.value().value().value(), IsTestMessage(1));
  auto pull2 = reader.PullMessage();
  auto poll2 = pull2();
  EXPECT_TRUE(poll2.pending());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher2(), IsReady(0)));
  poll2 = pull2();
  ASSERT_TRUE(poll1.ready());
  ASSERT_TRUE(poll2.value().ok());
  ASSERT_TRUE(poll2.value().value().has_value());
  EXPECT_THAT(poll2.value().value().value(), IsTestMessage(2));
}

TEST(RequestBufferTest, SwitchAfterEndOfStream) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  buffer.Commit(&reader);
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  EXPECT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, NothingAfterEndOfStream) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  EXPECT_EQ(buffer.FinishSends(), Success{});
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
  auto pull_msg2 = reader.PullMessage();
  poll_msg = pull_msg2();
  ASSERT_TRUE(poll_msg.ready());
  ASSERT_TRUE(poll_msg.value().ok());
  EXPECT_FALSE(poll_msg.value().value().has_value());
}

TEST(RequestBufferTest, CancelBeforeInitialMetadataPush) {
  RequestBuffer buffer;
  buffer.Cancel();
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), Failure{});
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  auto poll_md = pull_md();
  ASSERT_THAT(poll_md, IsReady());
  ASSERT_FALSE(poll_md.value().ok());
}

TEST(RequestBufferTest, CancelBeforeInitialMetadataPull) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  buffer.Cancel();
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  auto poll_md = pull_md();
  ASSERT_THAT(poll_md, IsReady());
  ASSERT_FALSE(poll_md.value().ok());
}

TEST(RequestBufferTest, CancelBeforeMessagePush) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  buffer.Cancel();
  auto pusher = buffer.PushMessage(TestMessage());
  auto poll = pusher();
  ASSERT_THAT(poll, IsReady());
  ASSERT_FALSE(poll.value().ok());
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  auto poll_md = pull_md();
  ASSERT_THAT(poll_md, IsReady());
  ASSERT_FALSE(poll_md.value().ok());
}

TEST(RequestBufferTest, CancelBeforeMessagePushButAfterInitialMetadataPull) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), 40);
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  auto poll_md = pull_md();
  ASSERT_THAT(poll_md, IsReady());
  ASSERT_TRUE(poll_md.value().ok());
  EXPECT_THAT(*poll_md.value(), IsTestMetadata());
  buffer.Cancel();
  auto pusher = buffer.PushMessage(TestMessage());
  auto poll = pusher();
  ASSERT_THAT(poll, IsReady());
  ASSERT_FALSE(poll.value().ok());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
