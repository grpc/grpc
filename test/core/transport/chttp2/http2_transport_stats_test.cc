//
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
//

#include "src/core/ext/transport/chttp2/transport/http2_transport_stats.h"

#include <cstddef>
#include <variant>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/time.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace http2 {
namespace testing {

class Http2TransportStatsTest : public ::testing::Test {
 protected:
  Http2TransportStats stats_{ChannelArgs()};
  const WriteFrameStatsVisitor visitor_{stats_};
  const Http2GlobalStatsTestHelper stats_helper_;
};

// Verifies that Http2TransportStats initializes a non-null
// Http2StatsCollector with http2_writes_begun starting at zero.
TEST_F(Http2TransportStatsTest, InitializesStatsCollector) {
  ASSERT_NE(stats_.GetStatsCollector(), nullptr);
  EXPECT_EQ(stats_.GetStatsCollector()->View().http2_writes_begun, 0u);
}

TEST_F(Http2TransportStatsTest, RecordsDataFrameAndMessageSizes) {
  stats_.RecordReadDataFrameSize(128u);
  stats_.RecordSendMessageSize(512u);
  stats_.RecordWriteDataFrameSize(1024u);

  stats_helper_.ExpectReadDataFrameSize(/*value=*/128, /*expected_count=*/1u);
  stats_helper_.ExpectSendMessageSize(/*value=*/512, /*expected_count=*/1u);
  stats_helper_.ExpectWriteDataFrameSize(/*value=*/1024, /*expected_count=*/1u);
}

TEST_F(Http2TransportStatsTest, BeginWriteRecordsStats) {
  constexpr size_t kTargetWriteSize = 131072u;
  stats_.BeginWrite(kTargetWriteSize);

  stats_helper_.ExpectWritesBegun(/*expected_count=*/1u);
  stats_helper_.ExpectWriteTargetSize(
      /*value=*/static_cast<int>(kTargetWriteSize), /*expected_count=*/1u);
}

TEST_F(Http2TransportStatsTest, RecordsControlFrameAndStallCounters) {
  stats_.RecordSettingsWrites();
  stats_.RecordPingsSent();
  stats_.RecordPingsSent();
  stats_.MaybeRecordStall(chttp2::StallType::kTransportStalled);
  stats_.MaybeRecordStall(chttp2::StallType::kStreamStalled);
  stats_.MaybeRecordStall(chttp2::StallType::kStreamStalled);
  stats_.MaybeRecordStall(chttp2::StallType::kNone);

  stats_helper_.ExpectSettingsWrites(/*expected_count=*/1u);
  stats_helper_.ExpectPingsSent(/*expected_count=*/2u);
  stats_helper_.ExpectTransportStalls(/*expected_count=*/1u);
  stats_helper_.ExpectStreamStalls(/*expected_count=*/2u);
}

// Verifies that the transport window update period is only recorded starting
// from the second update (since last_transport_window_update_time_ starts at
// Timestamp::InfPast()).
TEST_F(Http2TransportStatsTest, RecordsTransportWindowUpdateSizeAndPeriod) {
  ExecCtx exec_ctx;

  // First transport WINDOW_UPDATE records increment, but not period.
  stats_.RecordTransportWindowUpdate(2048u);
  stats_helper_.ExpectTransportRemoteWindowUpdate(/*value=*/2048,
                                                  /*expected_count=*/1u);
  stats_helper_.ExpectTransportWindowUpdatePeriodTotalCount(
      /*expected_count=*/0.0);

  // Second transport WINDOW_UPDATE records both increment and period.
  stats_.RecordTransportWindowUpdate(4096u);
  stats_helper_.ExpectTransportRemoteWindowUpdate(/*value=*/4096,
                                                  /*expected_count=*/1u);
  stats_helper_.ExpectTransportWindowUpdatePeriodTotalCount(
      /*expected_count=*/1.0);
}

// Verifies that stream window update size is recorded on every call, and
// the period is recorded from the second update onward.
TEST_F(Http2TransportStatsTest, RecordsStreamWindowUpdateSizeAndPeriod) {
  ExecCtx exec_ctx;
  Timestamp last_window_update_time = Timestamp::InfPast();

  // First stream WINDOW_UPDATE updates last_window_update_time and
  // records increment, but does not record period.
  last_window_update_time = stats_.RecordStreamWindowUpdate(
      /*increment=*/1024u, last_window_update_time);
  EXPECT_NE(last_window_update_time, Timestamp::InfPast());
  stats_helper_.ExpectStreamRemoteWindowUpdate(/*value=*/1024,
                                               /*expected_count=*/1u);
  stats_helper_.ExpectStreamWindowUpdatePeriodTotalCount(
      /*expected_count=*/0.0);

  // Second stream WINDOW_UPDATE records both increment and period.
  last_window_update_time = stats_.RecordStreamWindowUpdate(
      /*increment=*/8192u, last_window_update_time);
  stats_helper_.ExpectStreamRemoteWindowUpdate(/*value=*/8192,
                                               /*expected_count=*/1u);
  stats_helper_.ExpectStreamWindowUpdatePeriodTotalCount(
      /*expected_count=*/1.0);
}

// Verifies that WriteFrameStatsVisitor only records metrics for DATA, non-ACK
// SETTINGS, and non-ACK PING frames, ignoring ACK frames and all other frame
// types.
TEST_F(Http2TransportStatsTest, WriteFrameStatsVisitorRecordsExpectedMetrics) {
  // Step 1: Visit all non-counted frame types and ACK control frames; verify
  // no stats are recorded.
  const Http2Frame ignored_frames[] = {
      Http2HeaderFrame{/*stream_id=*/1u, /*end_headers=*/true,
                       /*end_stream=*/false, SliceBuffer()},
      Http2ContinuationFrame{/*stream_id=*/1u, /*end_headers=*/true,
                             SliceBuffer()},
      Http2RstStreamFrame{/*stream_id=*/1u, /*error_code=*/0u},
      Http2SettingsFrame{/*ack=*/true, {}},
      Http2PingFrame{/*ack=*/true, /*opaque=*/100u},
      Http2GoawayFrame{/*last_stream_id=*/1u, /*error_code=*/0u, Slice()},
      Http2WindowUpdateFrame{/*stream_id=*/1u, /*increment=*/1000u},
      Http2SecurityFrame{SliceBuffer()},
      Http2UnknownFrame{},
      Http2EmptyFrame{},
  };
  for (const Http2Frame& frame : ignored_frames) {
    std::visit(visitor_, frame);
  }

  stats_helper_.ExpectWriteDataFrameSizeTotalCount(/*expected_count=*/0.0);
  stats_helper_.ExpectSettingsWrites(/*expected_count=*/0u);
  stats_helper_.ExpectPingsSent(/*expected_count=*/0u);

  // Step 2: Visit counted frames (DATA, non-ACK SETTINGS, non-ACK PING) and
  // verify their metrics.
  const Http2Frame data_frame =
      Http2DataFrame{/*stream_id=*/1u, /*end_stream=*/false,
                     SliceBuffer(Slice::ZeroContentsWithLength(256u))};
  const Http2Frame settings_frame = Http2SettingsFrame{/*ack=*/false, {}};
  const Http2Frame ping_frame = Http2PingFrame{/*ack=*/false, /*opaque=*/200u};

  std::visit(visitor_, data_frame);
  std::visit(visitor_, settings_frame);
  std::visit(visitor_, ping_frame);

  stats_helper_.ExpectWriteDataFrameSize(/*value=*/256, /*expected_count=*/1u);
  stats_helper_.ExpectSettingsWrites(/*expected_count=*/1u);
  stats_helper_.ExpectPingsSent(/*expected_count=*/1u);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
