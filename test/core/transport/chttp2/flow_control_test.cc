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

#include <gtest/gtest.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {
namespace chttp2 {

namespace {
auto* g_memory_owner = new MemoryOwner(
    ResourceQuota::Default()->memory_quota()->CreateMemoryOwner("test"));
}

TEST(FlowControl, NoOp) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  // Check initial values are per http2 spec
  EXPECT_EQ(tfc.sent_init_window(), 65535);
  EXPECT_EQ(tfc.acked_init_window(), 65535);
  EXPECT_EQ(tfc.remote_window(), 65535);
  EXPECT_EQ(tfc.target_frame_size(), 16384);
  EXPECT_EQ(sfc.remote_window_delta(), 0);
  EXPECT_EQ(sfc.min_progress_size(), 0);
  EXPECT_EQ(sfc.local_window_delta(), 0);
  EXPECT_EQ(sfc.announced_window_delta(), 0);
}

TEST(FlowControl, SendData) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  sfc.SentData(1024);
  EXPECT_EQ(sfc.remote_window_delta(), -1024);
  EXPECT_EQ(tfc.remote_window(), 65535 - 1024);
}

TEST(FlowControl, InitialTransportUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  EXPECT_EQ(tfc.MakeAction(), FlowControlAction());
}

TEST(FlowControl, InitialStreamUpdate) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  EXPECT_EQ(sfc.MakeAction(), FlowControlAction());
}

TEST(FlowControl, RecvData) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  EXPECT_EQ(absl::OkStatus(), sfc.RecvData(1024));
  EXPECT_EQ(tfc.announced_window(), 65535 - 1024);
  EXPECT_EQ(sfc.local_window_delta(), -1024);
}

TEST(FlowControl, TrackMinProgressSize) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  sfc.UpdateProgress(5);
  EXPECT_EQ(sfc.min_progress_size(), 5);
  sfc.UpdateProgress(10);
  EXPECT_EQ(sfc.min_progress_size(), 10);
  EXPECT_EQ(absl::OkStatus(), sfc.RecvData(5));
  EXPECT_EQ(sfc.min_progress_size(), 5);
  EXPECT_EQ(absl::OkStatus(), sfc.RecvData(5));
  EXPECT_EQ(sfc.min_progress_size(), 0);
  EXPECT_EQ(absl::OkStatus(), sfc.RecvData(5));
  EXPECT_EQ(sfc.min_progress_size(), 0);
}

TEST(FlowControl, NoUpdateWithoutReader) {
  ExecCtx exec_ctx;
  TransportFlowControl tfc("test", true, g_memory_owner);
  StreamFlowControl sfc(&tfc);
  for (int i = 0; i < 65535; i++) {
    EXPECT_EQ(sfc.RecvData(1), absl::OkStatus());
    EXPECT_EQ(sfc.MakeAction().send_stream_update(),
              FlowControlAction::Urgency::NO_ACTION_NEEDED);
  }
  // Empty window needing 1 byte to progress should trigger an immediate read.
  sfc.UpdateProgress(1);
  EXPECT_EQ(sfc.min_progress_size(), 1);
  EXPECT_EQ(sfc.MakeAction().send_stream_update(),
            FlowControlAction::Urgency::UPDATE_IMMEDIATELY);
  EXPECT_GT(sfc.MaybeSendUpdate(), 0);
}

}  // namespace chttp2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
