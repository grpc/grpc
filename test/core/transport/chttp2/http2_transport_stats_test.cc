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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/time.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace http2 {
namespace testing {

// Verifies that Http2TransportStats initializes a non-null Http2StatsCollector
// with zero initial per-transport Http2Stats counters.
TEST(Http2TransportStatsTest, InitializesStatsCollector) {
  const Http2TransportStats stats((ChannelArgs()));
  ASSERT_NE(stats.GetStatsCollector(), nullptr);
  EXPECT_EQ(stats.GetStatsCollector()->View().http2_writes_begun, 0u);
}

// Verifies that RecordReadDataFrameSize, RecordSendMessageSize, and
// RecordWriteDataFrameSize record payload sizes into their corresponding
// Http2GlobalStats histograms.
TEST(Http2TransportStatsTest, RecordsDataFrameAndMessageSizes) {
  Http2TransportStats stats((ChannelArgs()));
  const Http2GlobalStatsTestHelper stats_helper;

  stats.RecordReadDataFrameSize(128u);
  stats.RecordSendMessageSize(512u);
  stats.RecordWriteDataFrameSize(1024u);

  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2ReadDataFrameSize, 128, 1u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2SendMessageSize, 512, 1u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2WriteDataFrameSize, 1024, 1u);
}

// Verifies that RecordWritesBegun and RecordWriteTargetSize update both the
// per-transport Http2Stats view and the process-wide Http2GlobalStats.
TEST(Http2TransportStatsTest, RecordsWriteCycleStatsLocalAndGlobal) {
  Http2TransportStats stats((ChannelArgs()));
  const Http2GlobalStatsTestHelper stats_helper;
  constexpr size_t kTargetWriteSize = 131072u;

  stats.RecordWritesBegun();
  stats.RecordWriteTargetSize(kTargetWriteSize);

  // 1. Verify per-transport Http2Stats view.
  const Http2Stats& local_view = stats.GetStatsCollector()->View();
  EXPECT_EQ(local_view.http2_writes_begun, 1u);
  const int local_bucket = local_view.http2_write_target_size.BucketFor(
      static_cast<int>(kTargetWriteSize));
  EXPECT_EQ(local_view.http2_write_target_size.buckets()[local_bucket], 1u);

  // 2. Verify global Http2GlobalStats diff.
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2WritesBegun,
                                 1u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2WriteTargetSize,
      static_cast<int>(kTargetWriteSize), 1u);
}

// Verifies that RecordSettingsWrites, RecordPingsSent, RecordTransportStalls,
// and RecordStreamStalls increment their respective Http2GlobalStats counters.
TEST(Http2TransportStatsTest, RecordsControlFrameAndStallCounters) {
  Http2TransportStats stats((ChannelArgs()));
  const Http2GlobalStatsTestHelper stats_helper;

  stats.RecordSettingsWrites();
  stats.RecordPingsSent();
  stats.RecordPingsSent();
  stats.RecordTransportStalls();
  stats.RecordStreamStalls();
  stats.RecordStreamStalls();

  stats_helper.ExpectCounterDiff(
      Http2GlobalStats::Counter::kHttp2SettingsWrites, 1u);
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2PingsSent,
                                 2u);
  stats_helper.ExpectCounterDiff(
      Http2GlobalStats::Counter::kHttp2TransportStalls, 1u);
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2StreamStalls,
                                 2u);
}

// Verifies that RecordTransportWindowUpdate records the window size increment
// on every call, and records the transport window update period only starting
// from the second call (since last_transport_window_update_time_ starts at
// Timestamp::InfPast()).
TEST(Http2TransportStatsTest, RecordsTransportWindowUpdateSizeAndPeriod) {
  ExecCtx exec_ctx;
  Http2TransportStats stats((ChannelArgs()));
  const Http2GlobalStatsTestHelper stats_helper;

  // Step 1: First transport WINDOW_UPDATE records increment, but not period.
  stats.RecordTransportWindowUpdate(2048u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2TransportRemoteWindowUpdate, 2048, 1u);
  stats_helper.ExpectHistogramTotalCountDiff(
      Http2GlobalStats::Histogram::kHttp2TransportWindowUpdatePeriod, 0.0);

  // Step 2: Second transport WINDOW_UPDATE records both increment and period.
  stats.RecordTransportWindowUpdate(4096u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2TransportRemoteWindowUpdate, 4096, 1u);
  stats_helper.ExpectHistogramTotalCountDiff(
      Http2GlobalStats::Histogram::kHttp2TransportWindowUpdatePeriod, 1.0);
}

// Verifies that RecordStreamWindowUpdate updates the caller-supplied
// last_window_update_time, records the window size increment on every call,
// and records the stream window update period only starting from the second
// call on that stream.
TEST(Http2TransportStatsTest, RecordsStreamWindowUpdateSizeAndPeriod) {
  ExecCtx exec_ctx;
  Http2TransportStats stats((ChannelArgs()));
  const Http2GlobalStatsTestHelper stats_helper;
  Timestamp last_window_update_time = Timestamp::InfPast();

  // Step 1: First stream WINDOW_UPDATE updates last_window_update_time and
  // records increment, but does not record period.
  stats.RecordStreamWindowUpdate(1024u, last_window_update_time);
  EXPECT_NE(last_window_update_time, Timestamp::InfPast());
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2StreamRemoteWindowUpdate, 1024, 1u);
  stats_helper.ExpectHistogramTotalCountDiff(
      Http2GlobalStats::Histogram::kHttp2StreamWindowUpdatePeriod, 0.0);

  // Step 2: Second stream WINDOW_UPDATE records both increment and period.
  stats.RecordStreamWindowUpdate(8192u, last_window_update_time);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2StreamRemoteWindowUpdate, 8192, 1u);
  stats_helper.ExpectHistogramTotalCountDiff(
      Http2GlobalStats::Histogram::kHttp2StreamWindowUpdatePeriod, 1.0);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
