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

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <memory>
#include <tuple>

#include "gtest/gtest.h"

#include <grpc/support/time.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/bdp_estimator.h"

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_core {
namespace chttp2 {

namespace {

constexpr uint64_t kMaxAdvanceTimeMillis = 24ull * 365 * 3600 * 1000;

gpr_timespec g_now;
gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  gpr_timespec ts = g_now;
  ts.clock_type = clock_type;
  return ts;
}

void InitGlobals() {
  g_now = {1, 0, GPR_CLOCK_MONOTONIC};
  TestOnlySetProcessEpoch(g_now);
  gpr_now_impl = now_impl;
}

void AdvanceClockMillis(uint64_t millis) {
  ExecCtx exec_ctx;
  g_now = gpr_time_add(g_now, gpr_time_from_millis(Clamp(millis, uint64_t{1},
                                                         kMaxAdvanceTimeMillis),
                                                   GPR_TIMESPAN));
  exec_ctx.InvalidateNow();
}

class TransportTargetWindowEstimatesMocker
    : public chttp2::TestOnlyTransportTargetWindowEstimatesMocker {
 public:
  explicit TransportTargetWindowEstimatesMocker() {}

  double ComputeNextTargetInitialWindowSizeFromPeriodicUpdate(
      double current_target) override {
    const double kSmallWindow = 16384;
    const double kBigWindow = 1024 * 1024;
    // Bounce back and forth between small and big initial windows.
    if (current_target > kSmallWindow) {
      return kSmallWindow;
    } else {
      return kBigWindow;
    }
  }
};

}  // namespace

class FlowControlTest : public ::testing::Test {
 protected:
  MemoryOwner memory_owner_ = MemoryOwner(
      ResourceQuota::Default()->memory_quota()->CreateMemoryOwner("test"));
};

TEST_F(FlowControlTest, NoOp) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  // Check initial values are per http2 spec
  EXPECT_EQ(tfc.acked_init_window(), 65535);
  EXPECT_EQ(tfc.remote_window(), 65535);
  EXPECT_EQ(tfc.target_frame_size(), 16384);
  EXPECT_EQ(tfc.target_preferred_rx_crypto_frame_size(), INT_MAX);
  EXPECT_EQ(sfc.remote_window_delta(), 0);
  EXPECT_EQ(sfc.min_progress_size(), 0);
  EXPECT_EQ(sfc.announced_window_delta(), 0);
}

TEST_F(FlowControlTest, SendData) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  int64_t prev_preferred_rx_frame_size =
      tfc.target_preferred_rx_crypto_frame_size();
  {
    StreamFlowControl::OutgoingUpdateContext sfc_upd(&sfc);
    sfc_upd.SentData(1024);
  }
  EXPECT_EQ(sfc.remote_window_delta(), -1024);
  EXPECT_EQ(tfc.remote_window(), 65535 - 1024);
  EXPECT_EQ(tfc.target_preferred_rx_crypto_frame_size(),
            prev_preferred_rx_frame_size);
}

TEST_F(FlowControlTest, InitialTransportUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  EXPECT_EQ(TransportFlowControl::IncomingUpdateContext(&tfc).MakeAction(),
            FlowControlAction());
}

TEST_F(FlowControlTest, InitialStreamUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  EXPECT_EQ(StreamFlowControl::IncomingUpdateContext(&sfc).MakeAction(),
            FlowControlAction());
}

TEST_F(FlowControlTest, PeriodicUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  constexpr int kNumPeriodicUpdates = 100;
  Timestamp next_ping = Timestamp::Now() + Duration::Milliseconds(1000);
  uint32_t prev_max_frame_size = tfc.target_frame_size();
  for (int i = 0; i < kNumPeriodicUpdates; i++) {
    BdpEstimator* bdp = tfc.bdp_estimator();
    bdp->AddIncomingBytes(1024 + (i * 100));
    // Advance clock till the timestamp of the next ping.
    AdvanceClockMillis((next_ping - Timestamp::Now()).millis());
    bdp->SchedulePing();
    bdp->StartPing();
    AdvanceClockMillis(10);
    next_ping = bdp->CompletePing();
    FlowControlAction action = tfc.PeriodicUpdate();
    if (IsTcpFrameSizeTuningEnabled()) {
      if (action.send_max_frame_size_update() !=
          FlowControlAction::Urgency::NO_ACTION_NEEDED) {
        prev_max_frame_size = action.max_frame_size();
      }
      EXPECT_EQ(action.preferred_rx_crypto_frame_size(),
                Clamp(2 * prev_max_frame_size, 16384u, 0x7fffffffu));
      EXPECT_TRUE(action.preferred_rx_crypto_frame_size_update() !=
                  FlowControlAction::Urgency::NO_ACTION_NEEDED);
    } else {
      EXPECT_EQ(action.preferred_rx_crypto_frame_size(), 0);
      EXPECT_TRUE(action.preferred_rx_crypto_frame_size_update() ==
                  FlowControlAction::Urgency::NO_ACTION_NEEDED);
    }
  }
}

TEST_F(FlowControlTest, RecvData) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
  int64_t prev_preferred_rx_frame_size =
      tfc.target_preferred_rx_crypto_frame_size();
  EXPECT_EQ(absl::OkStatus(), sfc_upd.RecvData(1024));
  std::ignore = sfc_upd.MakeAction();
  EXPECT_EQ(tfc.announced_window(), 65535 - 1024);
  EXPECT_EQ(sfc.announced_window_delta(), -1024);
  EXPECT_EQ(tfc.target_preferred_rx_crypto_frame_size(),
            prev_preferred_rx_frame_size);
}

TEST_F(FlowControlTest, TrackMinProgressSize) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    sfc_upd.SetMinProgressSize(5);
    std::ignore = sfc_upd.MakeAction();
  }
  EXPECT_EQ(sfc.min_progress_size(), 5);
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    sfc_upd.SetMinProgressSize(10);
    std::ignore = sfc_upd.MakeAction();
  }
  EXPECT_EQ(sfc.min_progress_size(), 10);
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    EXPECT_EQ(absl::OkStatus(), sfc_upd.RecvData(5));
    std::ignore = sfc_upd.MakeAction();
  }
  EXPECT_EQ(sfc.min_progress_size(), 5);
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    EXPECT_EQ(absl::OkStatus(), sfc_upd.RecvData(5));
    std::ignore = sfc_upd.MakeAction();
  }
  EXPECT_EQ(sfc.min_progress_size(), 0);
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    EXPECT_EQ(absl::OkStatus(), sfc_upd.RecvData(5));
    std::ignore = sfc_upd.MakeAction();
  }
  EXPECT_EQ(sfc.min_progress_size(), 0);
}

TEST_F(FlowControlTest, NoUpdateWithoutReader) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  for (int i = 0; i < 65535; i++) {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    EXPECT_EQ(sfc_upd.RecvData(1), absl::OkStatus());
    EXPECT_EQ(sfc_upd.MakeAction().send_stream_update(),
              FlowControlAction::Urgency::NO_ACTION_NEEDED);
  }
  // Empty window needing 1 byte to progress should trigger an immediate read.
  {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    sfc_upd.SetMinProgressSize(1);
    EXPECT_EQ(sfc.min_progress_size(), 1);
    EXPECT_EQ(sfc_upd.MakeAction().send_stream_update(),
              FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
  }
  EXPECT_GT(tfc.MaybeSendUpdate(false), 0);
  EXPECT_GT(sfc.MaybeSendUpdate(), 0);
}

TEST_F(FlowControlTest, GradualReadsUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, &memory_owner_);
  StreamFlowControl sfc(&tfc);
  int immediate_updates = 0;
  int queued_updates = 0;
  for (int i = 0; i < 65535; i++) {
    StreamFlowControl::IncomingUpdateContext sfc_upd(&sfc);
    EXPECT_EQ(sfc_upd.RecvData(1), absl::OkStatus());
    sfc_upd.SetPendingSize(0);
    switch (sfc_upd.MakeAction().send_stream_update()) {
      case FlowControlAction::Urgency::UPDATE_IMMEDIATELY:
        immediate_updates++;
        break;
      case FlowControlAction::Urgency::QUEUE_UPDATE:
        queued_updates++;
        break;
      case FlowControlAction::Urgency::NO_ACTION_NEEDED:
        break;
    }
  }
  EXPECT_GE(immediate_updates, 0);
  EXPECT_GT(queued_updates, 0);
  EXPECT_EQ(immediate_updates + queued_updates, 65535);
}

}  // namespace chttp2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::chttp2::g_test_only_transport_target_window_estimates_mocker =
      new grpc_core::chttp2::TransportTargetWindowEstimatesMocker();
  grpc_core::chttp2::InitGlobals();
  return RUN_ALL_TESTS();
}
