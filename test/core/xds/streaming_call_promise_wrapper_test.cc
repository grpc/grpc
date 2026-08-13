//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"

#include <grpc/grpc.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/promise/test_wakeup_schedulers.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

using grpc_event_engine::experimental::FuzzingEventEngine;

namespace grpc_core {
namespace {

constexpr char kMethod[] = "/test.Method";
constexpr char kClientMessage[] = "hello world";
constexpr char kServerMessage[] = "server response";

class FakeXdsServerTarget : public XdsBootstrap::XdsServerTarget {
 public:
  explicit FakeXdsServerTarget(std::string server_uri)
      : server_uri_(std::move(server_uri)) {}
  const std::string& server_uri() const override { return server_uri_; }
  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    const auto& o = DownCast<const FakeXdsServerTarget&>(other);
    return server_uri_ == o.server_uri_;
  }

 private:
  std::string server_uri_;
};

class XdsStreamingCallPromiseWrapperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Too many pending reads"; }, event_engine_);
    transport_factory_->SetAbortOnUndrainedMessages(false);
    target_ = std::make_unique<FakeXdsServerTarget>("localhost:1234");
  }

  void TearDown() override {
    wrapper_.reset();
    stream_.reset();
    transport_.reset();
    transport_factory_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
  }

  void InitStream(bool auto_complete_messages_from_client = true) {
    transport_factory_->SetAutoCompleteMessagesFromClient(
        auto_complete_messages_from_client);
    absl::Status status;
    transport_ = transport_factory_->GetTransport(*target_, &status);
    ASSERT_TRUE(status.ok()) << status;
    ASSERT_NE(transport_, nullptr);
    wrapper_ =
        MakeRefCounted<XdsStreamingCallPromiseWrapper>(*transport_, kMethod);
    stream_ = transport_factory_->WaitForStream(*target_, kMethod);
    ASSERT_NE(stream_, nullptr);
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  std::unique_ptr<FakeXdsServerTarget> target_;
  RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  RefCountedPtr<XdsStreamingCallPromiseWrapper> wrapper_;
  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> stream_;
};

TEST_F(XdsStreamingCallPromiseWrapperTest, PushMessageSuccess) {
  InitStream();
  bool send_completed = false;
  auto activity = MakeActivity(
      [this, &send_completed] {
        return Map(wrapper_->PushMessage(kClientMessage),
                   [&](StatusFlag status) {
                     EXPECT_TRUE(status.ok());
                     send_completed = true;
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [](const absl::Status& status) { EXPECT_TRUE(status.ok()) << status; });
  event_engine_->TickUntilIdle();
  EXPECT_TRUE(send_completed);
  EXPECT_EQ(stream_->WaitForMessageFromClient(), kClientMessage);
}

TEST_F(XdsStreamingCallPromiseWrapperTest, PushMessageFailure) {
  InitStream(/*auto_complete_messages_from_client=*/false);
  bool send_completed = false;
  auto activity = MakeActivity(
      [this, &send_completed] {
        return Map(wrapper_->PushMessage(kClientMessage),
                   [&](StatusFlag status) {
                     EXPECT_FALSE(status.ok());
                     send_completed = true;
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(), [](const absl::Status&) {});
  event_engine_->TickUntilIdle();
  EXPECT_FALSE(send_completed);
  stream_->CompleteSendMessageFromClient(false);
  event_engine_->TickUntilIdle();
  EXPECT_TRUE(send_completed);
}

TEST_F(XdsStreamingCallPromiseWrapperTest, PullMessageSuccess) {
  InitStream();
  std::optional<std::string> received_message;
  auto activity = MakeActivity(
      [this, &received_message] {
        return Map(wrapper_->PullMessage(),
                   [&](const std::optional<std::string>& res) {
                     received_message = res;
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [](const absl::Status& status) { EXPECT_TRUE(status.ok()) << status; });
  event_engine_->TickUntilIdle();
  EXPECT_FALSE(received_message.has_value());
  stream_->SendMessageToClient(kServerMessage);
  event_engine_->TickUntilIdle();
  ASSERT_TRUE(received_message.has_value());
  EXPECT_EQ(*received_message, kServerMessage);
}

TEST_F(XdsStreamingCallPromiseWrapperTest, PullMessageEndOfStream) {
  InitStream();
  bool pull_completed = false;
  std::optional<std::string> received_message;
  auto activity = MakeActivity(
      [this, &received_message, &pull_completed] {
        return Map(wrapper_->PullMessage(),
                   [&](const std::optional<std::string>& res) {
                     received_message = res;
                     pull_completed = true;
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [](const absl::Status& status) { EXPECT_TRUE(status.ok()) << status; });
  event_engine_->TickUntilIdle();
  EXPECT_FALSE(pull_completed);
  stream_->MaybeSendStatusToClient(absl::OkStatus());
  event_engine_->TickUntilIdle();
  EXPECT_TRUE(pull_completed);
  EXPECT_FALSE(received_message.has_value());
}

TEST_F(XdsStreamingCallPromiseWrapperTest, PullServerTrailingMetadata) {
  InitStream();
  absl::Status received_status = absl::UnknownError("initial");
  auto activity = MakeActivity(
      [this, &received_status] {
        return Map(wrapper_->PullServerTrailingMetadata(),
                   [&](const absl::Status& status) {
                     received_status = status;
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [](const absl::Status& status) { EXPECT_TRUE(status.ok()) << status; });
  event_engine_->TickUntilIdle();
  EXPECT_EQ(received_status, absl::UnknownError("initial"));
  stream_->MaybeSendStatusToClient(absl::UnavailableError("unavailable"));
  event_engine_->TickUntilIdle();
  EXPECT_EQ(received_status, absl::UnavailableError("unavailable"));
}

TEST_F(XdsStreamingCallPromiseWrapperTest, SendHalfClose) {
  InitStream();
  EXPECT_FALSE(stream_->half_closed());
  wrapper_->SendHalfClose();
  event_engine_->TickUntilIdle();
  EXPECT_TRUE(stream_->half_closed());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_timer_manager_set_start_threaded(false);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
