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
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), Success{});
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
  EXPECT_WAKEUP(
      activity,
      EXPECT_EQ(buffer.PushClientInitialMetadata(std::move(md)), Success{}));
  poll = poller();
  ASSERT_THAT(poll, IsReady());
  auto value = std::move(poll.value());
  ASSERT_TRUE(value.ok());
  EXPECT_THAT(*value, IsTestMetadata());
}

TEST(RequestBufferTest, PushThenPullMessage) {
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), Success{});
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_THAT(pusher(), IsReady(49));
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  ASSERT_THAT(poll_msg, IsReady());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

TEST(RequestBufferTest, PullThenPushMessage) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  RequestBuffer buffer;
  EXPECT_EQ(buffer.PushClientInitialMetadata(TestMetadata()), Success{});
  RequestBuffer::Reader reader(&buffer);
  auto pull_md = reader.PullClientInitialMetadata();
  EXPECT_THAT(pull_md(), IsReady());  // value tested elsewhere
  auto pull_msg = reader.PullMessage();
  auto poll_msg = pull_msg();
  EXPECT_THAT(poll_msg, IsPending());
  auto pusher = buffer.PushMessage(TestMessage());
  EXPECT_WAKEUP(activity, EXPECT_THAT(pusher(), IsReady(49)));
  poll_msg = pull_msg();
  ASSERT_THAT(poll_msg, IsReady());
  ASSERT_TRUE(poll_msg.value().ok());
  ASSERT_TRUE(poll_msg.value().value().has_value());
  EXPECT_THAT(poll_msg.value().value().value(), IsTestMessage());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
