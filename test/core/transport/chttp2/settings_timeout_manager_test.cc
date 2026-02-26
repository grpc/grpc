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
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
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
using ::testing::MockFunction;

class SettingsPromiseManagerTest : public ::testing::Test {
 public:
  SettingsPromiseManagerTest() {
    transport_write_context_.StartWriteCycle();
    // Discard the connection preface
    MaybeFlushWriteBuffer();
  }

 protected:
  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

  http2::WriteCycle& GetWriteCycle() {
    return transport_write_context_.GetWriteCycle();
  }

 private:
  void MaybeFlushWriteBuffer() {
    if (transport_write_context_.GetWriteCycle().CanSerializeRegularFrames()) {
      bool unused;
      SliceBuffer discard =
          transport_write_context_.GetWriteCycle().SerializeRegularFrames(
              {unused});
    }
  }

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  http2::TransportWriteContext transport_write_context_;
};

constexpr uint32_t kSettingsShortTimeout = 500;
constexpr uint32_t kSettingsLongTimeoutTest = 2000;

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
  return TrySeq(Sleep(Duration::Milliseconds(kSettingsShortTimeout * 0.4)),
                [&manager]() -> Poll<absl::Status> {
                  LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
                  manager.TestOnlyRecordReceivedAck();
                  return absl::OkStatus();
                });
}

void AppendSettingsAckFrame(SliceBuffer& buf) {
  Http2SettingsFrame settings;
  settings.ack = true;
  Http2Frame frame(settings);
  Serialize(absl::Span<Http2Frame>(&frame, 1), buf);
}

void AppendSettingsFrame(SliceBuffer& buf,
                         std::vector<Http2SettingsFrame::Setting> settings) {
  Http2SettingsFrame frame_struct;
  frame_struct.ack = false;
  frame_struct.settings = std::move(settings);
  Http2Frame frame(std::move(frame_struct));
  Serialize(absl::Span<Http2Frame>(&frame, 1), buf);
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutOneSetting) {
  // First start the timer and then immediately send the ACK
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                              MockSettingsAckReceived(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettings) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
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
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettingsDelayed) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
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
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
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
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsPromiseManagerTest",
      TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                              MockStartSettingsTimeout(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
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
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
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
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, NoTimeoutThreeSettingsMixedOrder) {
  auto party = MakeParty();
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
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
        if (!status.ok()) {
          LOG(ERROR) << "Status " << status.status().message();
        }
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, TimeoutOneSetting) {
  // Testing one timeout test
  // Also ensuring that receiving the ACK after the timeout does not crash or
  // leak memory.
  auto party = MakeParty();
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification1;
  Notification notification2;
  party->Spawn("SettingsPromiseManagerTestStart",
               MockStartSettingsTimeout(manager),
               [&notification1](absl::Status status) {
                 EXPECT_TRUE(absl::IsCancelled(status));
                 EXPECT_THAT(status.message(),
                             ::testing::StartsWith(RFC9113::kSettingsTimeout));
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

TEST_F(SettingsPromiseManagerTest, MaybeGetSettingsAndSettingsAckFramesIdle) {
  // Tests that in idle state, first call to
  // MaybeGetSettingsAndSettingsAckFrames sends initial settings, and second
  // call does nothing.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>(/*on_receive_settings=*/nullptr);
  SliceBuffer output_buf;
  // We add "hello" to output_buf to ensure that
  // MaybeGetSettingsAndSettingsAckFrames appends to it and does not overwrite
  // it, i.e. the original contents of output_buf are not erased.
  output_buf.Append(Slice::FromCopiedString("hello"));
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  ASSERT_THAT(output_buf.JoinIntoString(), ::testing::StartsWith("hello"));
  EXPECT_GT(output_buf.Length(), 5);
  output_buf.Clear();
  output_buf.Append(Slice::FromCopiedString("hello"));
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 5);
  EXPECT_EQ(output_buf.JoinIntoString(), "hello");
}

TEST_F(SettingsPromiseManagerTest,
       MaybeGetSettingsAndSettingsAckFramesMultipleAcks) {
  // If multiple settings frames are received then multiple ACKs should be sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>(/*on_receive_settings=*/nullptr);
  SliceBuffer output_buf;
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  output_buf.Clear();
  for (int i = 0; i < 5; ++i) {
    timeout_manager->BufferPeerSettings(
        {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  }

  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }

  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());

  SliceBuffer expected_buf;
  for (int i = 0; i < 5; ++i) {
    AppendSettingsAckFrame(expected_buf);
  }
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());
}

TEST_F(SettingsPromiseManagerTest,
       MaybeGetSettingsAndSettingsAckFramesAfterAckAndChange) {
  // Tests that after initial settings are sent and ACKed, no frame is sent. If
  // settings are changed, a new SETTINGS frame with diff is sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>(/*on_receive_settings=*/nullptr);
  const uint32_t kSetMaxFrameSize = 16385;
  SliceBuffer output_buf;
  bool should_spawn_security_frame_loop = false;

  // Initial settings
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  EXPECT_GT(output_buf.Length(), 0);
  // Buffer peer settings that need to be ACKed. Because the first frame sent by
  // the peer will be a SETTINGS frame.
  timeout_manager->BufferPeerSettings(
      {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  timeout_manager->MaybeReportAndApplyBufferedPeerSettings(
      nullptr, should_spawn_security_frame_loop);

  // Section 2: Settings ACK received from peer
  EXPECT_TRUE(timeout_manager->OnSettingsAckReceived());
  output_buf.Clear();

  // No changes to local settings, but expecting a peer settings ACK frame.
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 9);
  SliceBuffer expected_buf;
  AppendSettingsAckFrame(expected_buf);
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());

  // Section 3: Local settings changed
  // If local settings are changed, MaybeGetSettingsAndSettingsAckFrames should
  // send a new SETTINGS frame with the diff.
  output_buf.Clear();
  expected_buf.Clear();
  // Change settings
  timeout_manager->mutable_local().SetMaxFrameSize(kSetMaxFrameSize);
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  // Check settings frame
  AppendSettingsFrame(expected_buf,
                      {{Http2Settings::kMaxFrameSizeWireId, kSetMaxFrameSize}});
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());

  // Section 4: Local settings set to same value
  // We set SetMaxFrameSize to the same value as previous value.
  // The Diff will be zero, in this case a new SETTINGS frame must not be sent.
  timeout_manager->mutable_local().SetMaxFrameSize(kSetMaxFrameSize);
  output_buf.Clear();
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_FALSE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  EXPECT_EQ(output_buf.Length(), 0);
}

TEST_F(SettingsPromiseManagerTest,
       MaybeGetSettingsAndSettingsAckFramesWithAck) {
  // Tests that if we need to send initial settings and also ACK received
  // settings, both frames are sent.
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  RefCountedPtr<SettingsPromiseManager> timeout_manager =
      MakeRefCounted<SettingsPromiseManager>(/*on_receive_settings=*/nullptr);
  SliceBuffer output_buf;
  // We add "hello" to output_buf to ensure that
  // MaybeGetSettingsAndSettingsAckFrames appends to it and does not overwrite
  // it, i.e. the original contents of output_buf are not erased.
  output_buf.Append(Slice::FromCopiedString("hello"));
  timeout_manager->BufferPeerSettings(
      {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  {
    http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
    timeout_manager->MaybeGetSettingsAndSettingsAckFrames(
        transport_flow_control, frame_sender);
    if (GetWriteCycle().CanSerializeRegularFrames()) {
      bool should_reset = false;
      output_buf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
    }
  }
  EXPECT_TRUE(timeout_manager->ShouldSpawnWaitForSettingsTimeout());
  timeout_manager->TestOnlyTimeoutWaiterSpawned();
  Http2SettingsFrame expected_settings;
  timeout_manager->mutable_local().Diff(
      true, Http2Settings(), [&](uint16_t key, uint32_t value) {
        expected_settings.settings.push_back({key, value});
      });
  SliceBuffer expected_buf;
  expected_buf.Append(Slice::FromCopiedString("hello"));
  AppendSettingsFrame(expected_buf, std::move(expected_settings.settings));
  AppendSettingsAckFrame(expected_buf);
  EXPECT_EQ(output_buf.Length(), expected_buf.Length());
  EXPECT_EQ(output_buf.JoinIntoString(), expected_buf.JoinIntoString());
}

TEST_F(SettingsPromiseManagerTest, OnReceiveSettingsCalled) {
  ExecCtx exec_ctx;
  Notification notification;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  bool should_spawn_security_frame_loop = false;
  SettingsPromiseManager manager(
      [&notification](absl::StatusOr<uint32_t> status) {
        ASSERT_TRUE(status.ok());
        EXPECT_EQ(status.value(), 100u);
        notification.Notify();
      });
  manager.BufferPeerSettings(
      {{Http2Settings::kMaxConcurrentStreamsWireId, 100u}});
  manager.MaybeReportAndApplyBufferedPeerSettings(
      event_engine.get(), should_spawn_security_frame_loop);
  notification.WaitForNotification();
}

TEST_F(SettingsPromiseManagerTest, IsFirstPeerSettingsAppliedTest) {
  bool should_spawn_security_frame_loop = false;
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  EXPECT_FALSE(manager.IsFirstPeerSettingsApplied());

  manager.BufferPeerSettings(
      {{Http2Settings::kMaxConcurrentStreamsWireId, 100}});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_TRUE(manager.IsFirstPeerSettingsApplied());
}

TEST_F(SettingsPromiseManagerTest, IsSecurityFrameExpectedTest) {
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  bool should_spawn_security_frame_loop = false;

  // Make IsFirstPeerSettingsApplied true.
  manager.BufferPeerSettings({});
  manager.MaybeReportAndApplyBufferedPeerSettings(
      nullptr, should_spawn_security_frame_loop);
  EXPECT_TRUE(manager.IsFirstPeerSettingsApplied());

  // Defaults: peer allow = false.
  EXPECT_FALSE(manager.IsSecurityFrameExpected());

  // Set peer allow = true.
  manager.mutable_peer().SetAllowSecurityFrame(true);
  // local allow = false
  EXPECT_FALSE(manager.IsSecurityFrameExpected());

  // Set local allow = true.
  manager.mutable_local().SetAllowSecurityFrame(true);
  EXPECT_TRUE(manager.IsSecurityFrameExpected());

  // Reset local allow = false.
  manager.mutable_local().SetAllowSecurityFrame(false);
  EXPECT_FALSE(manager.IsSecurityFrameExpected());
}

TEST_F(SettingsPromiseManagerTest, ShouldSpawnSecurityFrameLoopTest) {
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  bool should_spawn_security_frame_loop = false;

  // Case 1: Default (false)
  manager.BufferPeerSettings({});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_FALSE(should_spawn_security_frame_loop);
}

TEST_F(SettingsPromiseManagerTest, ShouldSpawnSecurityFrameLoopTrueTest) {
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  bool should_spawn_security_frame_loop = false;

  // Case 2: Both True
  manager.mutable_local().SetAllowSecurityFrame(true);
  manager.BufferPeerSettings(
      {{Http2Settings::kGrpcAllowSecurityFrameWireId, 1}});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_TRUE(should_spawn_security_frame_loop);
}

TEST_F(SettingsPromiseManagerTest, ShouldSpawnSecurityFrameLoopFalseTest) {
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  bool should_spawn_security_frame_loop = false;

  // Case 3: Only Peer True
  manager.BufferPeerSettings(
      {{Http2Settings::kGrpcAllowSecurityFrameWireId, 1}});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_FALSE(should_spawn_security_frame_loop);
}

TEST_F(SettingsPromiseManagerTest, ShouldSpawnSecurityFrameLoopOnlyOnceTest) {
  SettingsPromiseManager manager(/*on_receive_settings=*/nullptr);
  bool should_spawn_security_frame_loop = false;

  // First time: Both True -> Spawn
  manager.mutable_local().SetAllowSecurityFrame(true);
  manager.BufferPeerSettings(
      {{Http2Settings::kGrpcAllowSecurityFrameWireId, 1}});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_TRUE(should_spawn_security_frame_loop);

  // Second time: Still True -> But logic only runs once -> Should be False
  // (untouched)
  should_spawn_security_frame_loop = false;  // Reset
  manager.BufferPeerSettings({});
  EXPECT_EQ(manager.MaybeReportAndApplyBufferedPeerSettings(
                nullptr, should_spawn_security_frame_loop),
            Http2ErrorCode::kNoError);
  EXPECT_FALSE(should_spawn_security_frame_loop);
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
