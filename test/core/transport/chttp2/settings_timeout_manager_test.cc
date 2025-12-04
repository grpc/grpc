//
//
// Copyright 2025 gRPC authors.
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;

class SettingsPromiseManagerTest : public ::testing::Test {
 protected:
  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

constexpr uint32_t kSettingsShortTimeout = 300;
constexpr uint32_t kSettingsLongTimeoutTest = 1400;

auto MockStartSettingsTimeout(SettingsPromiseManager& manager) {
  LOG(INFO) << "MockStartSettingsTimeout Factory";
  return manager.WaitForSettingsTimeout();
}

auto MockSettingsAckReceived(SettingsPromiseManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return [&manager]() -> Poll<absl::Status> {
    LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
    manager.TestOnlyRecordReceivedAck();
    return absl::OkStatus();
  };
}

auto MockSettingsAckReceivedDelayed(SettingsPromiseManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return TrySeq(Sleep(Duration::Milliseconds(kSettingsShortTimeout * 0.8)),
                [&manager]() -> Poll<absl::Status> {
                  LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
                  manager.TestOnlyRecordReceivedAck();
                  return absl::OkStatus();
                });
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutOneSetting) {
  // First start the timer and then immediately send the ACK
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                              MockSettingsAckReceived(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettings) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettingsDelayed) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutOneSettingRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                              MockStartSettingsTimeout(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettingsRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettingsMixedOrder) {
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, TimeoutOneSetting) {
  // Testing one timeout test
  // Also ensuring that receiving the ACK after the timeout does not crash or
  // leak memory.
  auto party = MakeParty();
  SettingsPromiseManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification1;
  Notification notification2;
  party->Spawn("SettingsPromiseManagerTestStart",
               MockStartSettingsTimeout(manager),
               [&notification1](absl::Status status) {
                 EXPECT_TRUE(absl::IsCancelled(status));
                 EXPECT_EQ(status.message(), RFC9113::kSettingsTimeout);
                 notification1.Notify();
               });
  party->Spawn(
      "SettingsPromiseManagerTestAck",
      TrySeq(Sleep(Duration::Milliseconds(kSettingsLongTimeoutTest)),
             MockSettingsAckReceived(manager)),
      [&notification2](absl::Status status) { notification2.Notify(); });
  notification1.WaitForNotification();
  notification2.WaitForNotification();
}

TEST(SettingsPromiseManagerTest1, MaybeGetSettingsAndSettingsAckFramesIdle) {
  // Tests that in idle state, first call to
  // MaybeGetSettingsAndSettingsAckFrames sends initial settings, and second
  // call does nothing.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>();
  SliceBuffer output_buf;
  // We add "hello" to output_buf to ensure that
  // MaybeGetSettingsAndSettingsAckFrames appends to it and does not overwrite
  // it, i.e. the original contents of output_buf are not erased.
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  ASSERT_THAT(output_buf.JoinIntoString(), ::testing::StartsWith("hello"));
  EXPECT_GT(output_buf.Length(), 5);
  output_buf.Clear();
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 5);
  EXPECT_EQ(output_buf.JoinIntoString(), "hello");
}

TEST(SettingsPromiseManagerTest1,
     MaybeGetSettingsAndSettingsAckFramesMultipleAcks) {
  // If multiple settings frames are received then multiple ACKs should be sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>();
  SliceBuffer output_buf;
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  output_buf.Clear();
  output_buf.Append(Slice::FromCopiedString("hello"));
  for (int i = 0; i < 5; ++i) {
    timeout_manager->BufferPeerSettings(
        {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  }

  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());

  SliceBuffer expected_buf;
  expected_buf.Append(Slice::FromCopiedString("hello"));
  for (int i = 0; i < 5; ++i) {
    Http2SettingsFrame settings;
    settings.ack = true;
    Http2Frame frame(settings);
    Serialize(absl::Span<Http2Frame>(&frame, 1), expected_buf);
  }
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());
}

TEST(SettingsPromiseManagerTest1,
     MaybeGetSettingsAndSettingsAckFramesAfterAckAndChange) {
  // Tests that after initial settings are sent and ACKed, no frame is sent. If
  // settings are changed, a new SETTINGS frame with diff is sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>();
  const uint32_t kSetMaxFrameSize = 16385;
  SliceBuffer output_buf;
  // We add "hello" to output_buf to ensure that
  // MaybeGetSettingsAndSettingsAckFrames appends to it and does not overwrite
  // it, i.e. the original contents of output_buf are not erased.
  output_buf.Append(Slice::FromCopiedString("hello"));
  // Initial settings
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  ASSERT_THAT(output_buf.JoinIntoString(), ::testing::StartsWith("hello"));
  EXPECT_GT(output_buf.Length(), 5);
  // Ack settings
  EXPECT_TRUE(timeout_manager->OnSettingsAckReceived());
  output_buf.Clear();
  output_buf.Append(Slice::FromCopiedString("hello"));
  // No changes - no frames
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 5);
  EXPECT_EQ(output_buf.JoinIntoString(), "hello");
  output_buf.Clear();
  // Change settings
  timeout_manager->mutable_local().SetMaxFrameSize(kSetMaxFrameSize);
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  // Check frame
  Http2SettingsFrame expected_settings;
  expected_settings.ack = false;
  expected_settings.settings.push_back(
      {Http2Settings::kMaxFrameSizeWireId, kSetMaxFrameSize});
  Http2Frame expected_frame(expected_settings);
  SliceBuffer expected_buf;
  expected_buf.Append(Slice::FromCopiedString("hello"));
  Serialize(absl::Span<Http2Frame>(&expected_frame, 1), expected_buf);
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());

  // We set SetMaxFrameSize to the same value as previous value.
  // The Diff will be zero, in this case a new SETTINGS frame must not be sent.
  timeout_manager->mutable_local().SetMaxFrameSize(kSetMaxFrameSize);
  output_buf.Clear();
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 5);
  EXPECT_EQ(output_buf.JoinIntoString(), "hello");
}

TEST(SettingsPromiseManagerTest1, MaybeGetSettingsAndSettingsAckFramesWithAck) {
  // Tests that if we need to send initial settings and also ACK received
  // settings, both frames are sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>();
  SliceBuffer output_buf;
  // We add "hello" to output_buf to ensure that
  // MaybeGetSettingsAndSettingsAckFrames appends to it and does not overwrite
  // it, i.e. the original contents of output_buf are not erased.
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->BufferPeerSettings(
      {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  timeout_manager->MaybeGetSettingsAndSettingsAckFrames(transport_flow_control,
                                                        output_buf);
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  Http2SettingsFrame expected_settings;
  expected_settings.ack = false;
  timeout_manager->mutable_local().Diff(
      true, Http2Settings(), [&](uint16_t key, uint32_t value) {
        expected_settings.settings.push_back({key, value});
      });
  Http2SettingsFrame expected_settings_ack;
  expected_settings_ack.ack = true;
  SliceBuffer expected_buf;
  expected_buf.Append(Slice::FromCopiedString("hello"));
  std::vector<Http2Frame> frames;
  frames.emplace_back(expected_settings);
  frames.emplace_back(expected_settings_ack);
  Serialize(absl::MakeSpan(frames), expected_buf);
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());
}

}  // namespace testing

}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}

// TODO(tjagtap) [PH2][P2] : New test :  SettingsPromiseManagerTest is getting
// constructed and destructed, on_receive_settings must be called.
