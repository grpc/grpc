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

#include "src/core/ext/transport/chttp2/transport/write_cycle.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport_stats.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/telemetry/stats_data.h"
#include "test/core/test_util/test_config.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {
namespace {

constexpr absl::string_view kData1 = "data1";
constexpr absl::string_view kData2 = "data2";
constexpr absl::string_view kData = "data";
// Size of a serialized RST_STREAM frame: (9-byte header + 4-byte error_code)
constexpr uint32_t kRstStreamFrameSize = 13u;

// This test verifies the initial state of WriteQuota.
// Assertions:
// - GetTargetWriteSize returns the constructor argument.
// - GetWriteBytesRemaining returns the target size initially (since consumption
//   is 0).
TEST(WriteQuotaTest, Initialization) {
  WriteQuota quota(100u);
  EXPECT_EQ(quota.GetTargetWriteSize(), 100u);
  EXPECT_EQ(quota.GetWriteBytesRemaining(), 100u);
}

// This test verifies that incrementing bytes consumed decreases the remaining
// write quota.
// Assertions:
// - GetWriteBytesRemaining decreases by the amount passed to
//   IncrementBytesConsumed.
TEST(WriteQuotaTest, Consumption) {
  WriteQuota quota(100u);
  quota.IncrementBytesConsumed(40u);
  EXPECT_EQ(quota.GetWriteBytesRemaining(), 60u);
  quota.IncrementBytesConsumed(30u);
  EXPECT_EQ(quota.GetWriteBytesRemaining(), 30u);
  EXPECT_EQ(quota.TestOnlyBytesConsumed(), 70u);
}

// This test verifies that GetWriteBytesRemaining returns 0 if bytes consumed
// exceeds target size.
// Assertions:
// - GetWriteBytesRemaining is 0 when consumed > target.
TEST(WriteQuotaTest, OverConsumption) {
  WriteQuota quota(100u);
  quota.IncrementBytesConsumed(110u);
  EXPECT_EQ(quota.GetWriteBytesRemaining(), 0u);
  EXPECT_EQ(quota.TestOnlyBytesConsumed(), 110u);
}

// This test verifies the initial state of WriteBufferTracker.
// Assertions:
// - CanSerializeUrgentFrames is false.
// - CanSerializeRegularFrames matches is_first_write.
// - RegularFrame counts are initially 0.
// - HasFirstWriteHappened is false.
class WriteBufferTrackerTest
    : public ::testing::TestWithParam<std::tuple<bool, bool>> {};

TEST_P(WriteBufferTrackerTest, Initialization) {
  bool is_first_write = std::get<0>(GetParam());
  const bool is_client = std::get<1>(GetParam());
  Http2TransportStats stats{ChannelArgs()};
  WriteBufferTracker tracker(is_first_write, is_client, stats);
  EXPECT_FALSE(tracker.CanSerializeUrgentFrames());
  EXPECT_EQ(tracker.CanSerializeRegularFrames(), is_first_write);
  EXPECT_EQ(tracker.GetRegularFrameCount(), 0u);
  EXPECT_EQ(tracker.GetUrgentFrameCount(), 0u);
  EXPECT_EQ(tracker.HasFirstWriteHappened(), !is_first_write);
}

// This test verifies adding default (non-urgent) frames to the tracker.
// Assertions:
// - GetRegularFrameCount increases on Add.
// - CanSerializeRegularFrames is true when frames are present.
TEST_P(WriteBufferTrackerTest, AddRegularFrames) {
  bool is_first_write = std::get<0>(GetParam());
  const bool is_client = std::get<1>(GetParam());
  Http2TransportStats stats{ChannelArgs()};
  WriteBufferTracker tracker(is_first_write, is_client, stats);
  Http2Frame frame1 = Http2DataFrame{
      1, /*end_stream=*/false, SliceBuffer(Slice::FromCopiedString(kData1))};
  tracker.AddRegularFrame(std::move(frame1));
  EXPECT_EQ(tracker.GetRegularFrameCount(), 1u);
  EXPECT_TRUE(tracker.CanSerializeRegularFrames());

  Http2Frame frame2 = Http2DataFrame{
      2, /*end_stream=*/false, SliceBuffer(Slice::FromCopiedString(kData2))};
  tracker.AddRegularFrame(std::move(frame2));
  EXPECT_EQ(tracker.GetRegularFrameCount(), 2u);
}

// This test verifies adding urgent frames to the tracker.
// Assertions:
// - GetUrgentFrameCount increases on AddUrgentFrame.
// - CanSerializeUrgentFrames is true.
TEST_P(WriteBufferTrackerTest, AddUrgentFrames) {
  bool is_first_write = std::get<0>(GetParam());
  const bool is_client = std::get<1>(GetParam());
  Http2TransportStats stats{ChannelArgs()};
  WriteBufferTracker tracker(is_first_write, is_client, stats);
  Http2Frame frame = Http2PingFrame{/*ack=*/false, 1234};
  EXPECT_FALSE(tracker.CanSerializeUrgentFrames());
  tracker.AddUrgentFrame(std::move(frame));
  EXPECT_EQ(tracker.GetUrgentFrameCount(), 1u);
  EXPECT_TRUE(tracker.CanSerializeUrgentFrames());
}

// This test verifies serialization of default frames.
// Assertions:
// - SerializeRegularFrames returns a non-empty buffer.
// - RegularFrame count is reset after serialization.
// - CanSerializeRegularFrames becomes false if it's not the first write and no
//   more frames.
TEST_P(WriteBufferTrackerTest, SerializeRegularFrames) {
  bool is_first_write = std::get<0>(GetParam());
  const bool is_client = std::get<1>(GetParam());
  Http2TransportStats stats{ChannelArgs()};
  WriteBufferTracker tracker(is_first_write, is_client, stats);

  Http2Frame frame = Http2DataFrame{
      1, /*end_stream=*/false, SliceBuffer(Slice::FromCopiedString(kData))};
  tracker.AddRegularFrame(std::move(frame));

  bool reset_ping_clock = false;
  SliceBuffer result = tracker.SerializeRegularFrames({reset_ping_clock});
  EXPECT_GT(result.Length(), 0u);
  EXPECT_EQ(tracker.GetRegularFrameCount(), 0u);
  EXPECT_FALSE(tracker.CanSerializeRegularFrames());
}

// This test verifies serialization of urgent frames.
// Assertions:
// - SerializeUrgentFrames returns a non-empty buffer.
// - Urgent frame count is reset after serialization.
// - HasUrgentFrames becomes false.
TEST_P(WriteBufferTrackerTest, SerializeUrgentFrames) {
  bool is_first_write = std::get<0>(GetParam());
  const bool is_client = std::get<1>(GetParam());
  Http2TransportStats stats{ChannelArgs()};
  WriteBufferTracker tracker(is_first_write, is_client, stats);
  Http2Frame frame = Http2PingFrame{/*ack=*/false, 1234};
  tracker.AddUrgentFrame(std::move(frame));

  bool reset_ping_clock = false;
  SliceBuffer result = tracker.SerializeUrgentFrames({reset_ping_clock});
  EXPECT_GT(result.Length(), 0u);
  EXPECT_EQ(tracker.GetUrgentFrameCount(), 0u);
  EXPECT_FALSE(tracker.CanSerializeUrgentFrames());
}

// This test verifies that is_first_write flag is updated after the first
// serialization.
// Assertions:
// - is_first_write is true initially.
// - is_first_write is false after SerializeRegularFrames.
TEST(WriteBufferTrackerTest, FirstWriteTransition) {
  Http2TransportStats stats{ChannelArgs()};
  for (bool is_client : {false, true}) {
    {
      bool is_first_write = true;
      WriteBufferTracker tracker(is_first_write, is_client, stats);

      tracker.AddRegularFrame(Http2DataFrame{
          1, false, SliceBuffer(Slice::FromCopiedString(kData))});
      bool reset = false;
      // SerializeRegularFrames will set is_first_write to false
      tracker.SerializeRegularFrames({reset});
      EXPECT_FALSE(is_first_write);
    }

    {
      bool is_first_write = true;
      WriteBufferTracker tracker(is_first_write, is_client, stats);

      tracker.AddUrgentFrame(Http2PingFrame{/*ack=*/false, 1234});
      bool reset = false;
      // SerializeUrgentFrames will set is_first_write to false
      tracker.SerializeUrgentFrames({reset});
      EXPECT_FALSE(is_first_write);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(WriteBufferTrackerTest, WriteBufferTrackerTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()));

// This test verifies that WriteCycle correctly delegates calls to WriteQuota
// and WriteBufferTracker. Assertions:
// - Quota is updated.
// - RegularFrame counts are correct.
// - Urgent frame availability is correctly reported.
// - Serialize methods clear their respective counts.
class WriteCycleTest : public ::testing::TestWithParam<bool> {};

TEST_P(WriteCycleTest, Delegation) {
  bool is_client = GetParam();
  Chttp2WriteSizePolicy policy;
  bool is_first_write = true;
  Http2TransportStats stats{ChannelArgs()};
  WriteCycle cycle(&policy, is_first_write, is_client, /*rst_streams=*/{},
                   stats);

  EXPECT_EQ(cycle.GetWriteBytesRemaining(), policy.WriteTargetSize());

  Http2Frame frame = Http2DataFrame{
      1, /*end_stream=*/false, SliceBuffer(Slice::FromCopiedString(kData))};
  size_t frame_size = GetFrameMemoryUsage(frame);
  cycle.GetFrameSender().AddRegularFrame(std::move(frame));

  EXPECT_EQ(cycle.GetRegularFrameCount(), 1u);
  EXPECT_EQ(cycle.GetWriteBytesRemaining(),
            policy.WriteTargetSize() - frame_size);

  Http2Frame urgent_frame = Http2PingFrame{false, 1234};
  cycle.write_buffer_tracker().AddUrgentFrame(std::move(urgent_frame));
  EXPECT_EQ(cycle.GetUrgentFrameCount(), 1u);
  EXPECT_TRUE(cycle.CanSerializeUrgentFrames());

  bool reset = false;
  SliceBuffer urgent_serialized = cycle.SerializeUrgentFrames({reset});
  EXPECT_GT(urgent_serialized.Length(), 0u);
  EXPECT_EQ(cycle.GetUrgentFrameCount(), 0u);

  SliceBuffer serialized = cycle.SerializeRegularFrames({reset});
  EXPECT_GT(serialized.Length(), 0u);
  EXPECT_EQ(cycle.GetRegularFrameCount(), 0u);

  cycle.BeginWrite(100u);
  cycle.EndWrite(true);
}

// This test covers remaining APIs of WriteCycle not covered in Delegation test.
// Assertions:
// - Initial counts are 0.
// - Counts and availability flags update correctly on
// AddRegularFrame/AddUrgentFrame.
TEST_P(WriteCycleTest, RemainingAPIs) {
  bool is_client = GetParam();
  Chttp2WriteSizePolicy policy;
  bool is_first_write = false;
  Http2TransportStats stats{ChannelArgs()};
  WriteCycle cycle(&policy, is_first_write, is_client, /*rst_streams=*/{},
                   stats);

  EXPECT_FALSE(cycle.CanSerializeUrgentFrames());
  EXPECT_EQ(cycle.GetUrgentFrameCount(), 0u);
  EXPECT_EQ(cycle.GetRegularFrameCount(), 0u);
  EXPECT_FALSE(cycle.CanSerializeRegularFrames());

  cycle.write_buffer_tracker().AddUrgentFrame(Http2PingFrame{false, 1234});
  EXPECT_TRUE(cycle.CanSerializeUrgentFrames());
  EXPECT_EQ(cycle.GetUrgentFrameCount(), 1u);

  cycle.write_buffer_tracker().AddRegularFrame(
      Http2DataFrame{1, false, SliceBuffer()});
  EXPECT_EQ(cycle.GetRegularFrameCount(), 1u);
  EXPECT_TRUE(cycle.CanSerializeRegularFrames());

  EXPECT_EQ(cycle.TestOnlyUrgentFrames().size(), 1u);
}

// This test verifies that WriteCycle's serialization sets the is_first_write
// flag to false. Assertions:
// - is_first_write is false after SerializeRegularFrames.
TEST_P(WriteCycleTest, SerializationSideEffects) {
  bool is_client = GetParam();
  Chttp2WriteSizePolicy policy;
  bool is_first_write = true;
  Http2TransportStats stats{ChannelArgs()};
  WriteCycle cycle(&policy, is_first_write, is_client, /*rst_streams=*/{},
                   stats);

  bool reset = false;
  const SliceBuffer serialized = cycle.SerializeRegularFrames({reset});
  EXPECT_FALSE(is_first_write);
}

TEST_P(WriteCycleTest, RstStreamAddedAndFlushed) {
  const bool is_client = GetParam();
  Chttp2WriteSizePolicy policy;
  bool is_first_write = true;
  Http2TransportStats stats{ChannelArgs()};

  std::vector<Http2RstStreamFrame> rst_streams = {
      Http2RstStreamFrame{/*stream_id=*/1u, /*error_code=*/2u}};

  WriteCycle cycle(&policy, is_first_write, is_client, std::move(rst_streams),
                   stats);

  // Verify that the RST_STREAM is added to the write buffer.
  EXPECT_EQ(cycle.GetRegularFrameCount(), 1u);

  bool reset = false;
  const SliceBuffer serialized = cycle.SerializeRegularFrames({reset});
  EXPECT_EQ(serialized.Length(),
            is_client
                ? (GRPC_CHTTP2_CLIENT_CONNECT_STRLEN + kRstStreamFrameSize)
                : kRstStreamFrameSize);
  // Verify that the RST_STREAM is flushed after serialization.
  EXPECT_EQ(cycle.GetRegularFrameCount(), 0u);
}

INSTANTIATE_TEST_SUITE_P(WriteCycleTest, WriteCycleTest, ::testing::Bool());

class TransportWriteContextTest : public ::testing::TestWithParam<bool> {
 protected:
  TransportWriteContextTest() : transport_write_context_(GetParam()) {}

  WriteCycle& GetWriteCycle() {
    return transport_write_context_.GetWriteCycle();
  }
  TransportWriteContext& GetTransportWriteContext() {
    return transport_write_context_;
  }

  void StartWriteCycle() {
    transport_write_context_.StartWriteCycle(http2_transport_stats_);
  }
  void StartWriteCycle(Http2TransportStats& http2_transport_stats) {
    transport_write_context_.StartWriteCycle(http2_transport_stats);
  }
  void EndWriteCycle() { transport_write_context_.EndWriteCycle(); }

 private:
  TransportWriteContext transport_write_context_;
  Http2TransportStats http2_transport_stats_{ChannelArgs()};
};

// This test verifies the initial state and DebugString of
// TransportWriteContext. Assertions:
// - IsFirstWrite is true initially.
// - DebugString is non-empty.
TEST_P(TransportWriteContextTest, DebugString) {
  TransportWriteContext& context = GetTransportWriteContext();
  EXPECT_TRUE(context.IsFirstWrite());
  EXPECT_FALSE(context.DebugString().empty());
}

TEST_P(TransportWriteContextTest, GetWriteArgsTest) {
  Http2Settings settings;
  // Default value of preferred_receive_crypto_message_size is 0, yields
  // INT_MAX for max_frame_size.
  PromiseEndpoint::WriteArgs args =
      TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(), INT_MAX);

  // If we set 0, it's clamped to min_preferred_receive_crypto_message_size.
  settings.SetPreferredReceiveCryptoMessageSize(0);
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::min_preferred_receive_crypto_message_size());

  // If we set 1024, it's clamped to min_preferred_receive_crypto_message_size.
  settings.SetPreferredReceiveCryptoMessageSize(1024);
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::min_preferred_receive_crypto_message_size());

  // If we set min_preferred_receive_crypto_message_size, it's clamped to
  // min_preferred_receive_crypto_message_size.
  settings.SetPreferredReceiveCryptoMessageSize(
      Http2Settings::min_preferred_receive_crypto_message_size());
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::min_preferred_receive_crypto_message_size());

  // If we set min_preferred_receive_crypto_message_size + 1, it's within range.
  settings.SetPreferredReceiveCryptoMessageSize(
      Http2Settings::min_preferred_receive_crypto_message_size() + 1);
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::min_preferred_receive_crypto_message_size() + 1);

  // If we set to max value, it's within range.
  settings.SetPreferredReceiveCryptoMessageSize(
      Http2Settings::max_preferred_receive_crypto_message_size());
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::max_preferred_receive_crypto_message_size());

  // If we set value > max value, it's clamped to max value.
  settings.SetPreferredReceiveCryptoMessageSize(
      Http2Settings::max_preferred_receive_crypto_message_size() + 1u);
  args = TransportWriteContext::GetWriteArgs(settings);
  EXPECT_EQ(args.max_frame_size(),
            Http2Settings::max_preferred_receive_crypto_message_size());
}

TEST_P(TransportWriteContextTest, WriteContextTest) {
  // 1. Initialize
  StartWriteCycle();
  WriteCycle& write_cycle = GetWriteCycle();
  size_t initial_target = write_cycle.GetWriteBytesRemaining();
  EXPECT_GT(initial_target, 0u);

  // 2. Consume bytes
  // We consume less than target to verify remaining calculation.
  write_cycle.GetFrameSender().AddRegularFrame(Http2SettingsFrame{});
  size_t bytes_consumed = GetFrameMemoryUsage(Http2SettingsFrame{});

  EXPECT_EQ(write_cycle.GetWriteBytesRemaining(),
            initial_target - bytes_consumed);

  // 3. Begin Write
  write_cycle.BeginWrite(bytes_consumed);

  // 4. End Write (Success)
  write_cycle.EndWrite(true);
  EndWriteCycle();

  // 5. Re-Initialize
  StartWriteCycle();
  WriteCycle& write_cycle2 = GetWriteCycle();
  EXPECT_GT(write_cycle2.GetWriteBytesRemaining(), 0u);

  // 6. Test Exceeding target (should clamp remaining to 0)
  write_cycle2.GetFrameSender().AddRegularFrame(
      Http2DataFrame{1, false,
                     SliceBuffer(Slice::ZeroContentsWithLength(
                         write_cycle2.GetWriteBytesRemaining() + 1u))});
  EXPECT_EQ(write_cycle2.GetWriteBytesRemaining(), 0u);

  write_cycle2.BeginWrite(100);
  write_cycle2.EndWrite(false);  // Fail
}

TEST_P(TransportWriteContextTest, QueuesAndSerializesRstStreams) {
  const bool is_client = GetParam();
  TransportWriteContext& context = GetTransportWriteContext();

  // Step 1: Queue a single RST_STREAM frame into TransportWriteContext.
  context.AddRstFrame(/*stream_id=*/1u, /*error_code=*/2u);

  // Step 2: Start the write cycle. This transfers queued RST frames into
  // WriteCycle.
  StartWriteCycle();
  WriteCycle& write_cycle = GetWriteCycle();

  // Step 3: Verify that the RST_STREAM frame is in the regular frame buffer.
  EXPECT_EQ(write_cycle.GetRegularFrameCount(), 1u);

  // Step 4: Serialize the regular frames and verify byte count.
  bool reset = false;
  const SliceBuffer serialized = write_cycle.SerializeRegularFrames({reset});
  EXPECT_EQ(serialized.Length(),
            is_client
                ? (GRPC_CHTTP2_CLIENT_CONNECT_STRLEN + kRstStreamFrameSize)
                : kRstStreamFrameSize);

  // Step 5: Verify that the frame is flushed after serialization.
  EXPECT_EQ(write_cycle.GetRegularFrameCount(), 0u);

  // Step 6: End the current write cycle.
  write_cycle.EndWrite(/*success=*/true);
  EndWriteCycle();

  // Step 7: Queue multiple RST_STREAM frames for the subsequent write cycle.
  context.AddRstFrame(/*stream_id=*/5u, /*error_code=*/1u);
  context.AddRstFrame(/*stream_id=*/7u, /*error_code=*/2u);
  context.AddRstFrame(/*stream_id=*/9u, /*error_code=*/3u);

  // Step 8: Start the next write cycle and verify all 3 frames are present.
  StartWriteCycle();
  WriteCycle& write_cycle2 = GetWriteCycle();
  EXPECT_EQ(write_cycle2.GetRegularFrameCount(), 3u);

  // Step 9: Serialize regular frames. Since is_first_write is now false,
  // no client connect string is prepended.
  const SliceBuffer multiple_serialized =
      write_cycle2.SerializeRegularFrames({reset});
  EXPECT_EQ(multiple_serialized.Length(), 3u * kRstStreamFrameSize);

  // Step 10: Verify the buffer is flushed and end the write cycle.
  EXPECT_EQ(write_cycle2.GetRegularFrameCount(), 0u);
  write_cycle2.EndWrite(/*success=*/true);
  EndWriteCycle();
}

// This test verifies that a full write cycle through TransportWriteContext and
// WriteCycle accurately records all 5 write-cycle HTTP/2 telemetry stats
// (writes begun, write target size, write data frame size, settings writes,
// and pings sent).
// Assertions:
// - StartWriteCycle increments http2_writes_begun by 1 and records
//   GetTargetWriteSize() in http2_write_target_size (both per-transport and
//   globally).
// - AddRegularFrame and AddUrgentFrame increment http2_settings_writes and
//   http2_pings_sent only for non-ACK frames, and record each Http2DataFrame's
//   payload length in http2_write_data_frame_size.
TEST_P(TransportWriteContextTest, RecordsAllWriteCycleStatsEndToEnd) {
  Http2TransportStats http2_transport_stats((ChannelArgs()));
  const testing::Http2GlobalStatsTestHelper stats_helper;

  // Step 1: Start the write cycle with http2_transport_stats, matching
  // MultiplexerLoop.
  StartWriteCycle(http2_transport_stats);
  WriteCycle& write_cycle = GetWriteCycle();
  const size_t target_write_size = write_cycle.GetTargetWriteSize();

  // Verify per-transport Http2Stats view for writes_begun and
  // write_target_size.
  const Http2Stats& transport_stats =
      http2_transport_stats.GetStatsCollector()->View();
  EXPECT_EQ(transport_stats.http2_writes_begun, 1u);
  const int local_target_bucket =
      transport_stats.http2_write_target_size.BucketFor(
          static_cast<int>(target_write_size));
  EXPECT_EQ(
      transport_stats.http2_write_target_size.buckets()[local_target_bucket],
      1u);

  // Verify that frame stats are 0 before frames are added to the write buffer.
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2WritesBegun,
                                 1u);
  stats_helper.ExpectCounterDiff(
      Http2GlobalStats::Counter::kHttp2SettingsWrites, 0u);
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2PingsSent,
                                 0u);

  // Step 2: Queue DATA, SETTINGS (non-ACK and ACK), and PING (non-ACK and ACK)
  // frames into the write cycle.
  FrameSender frame_sender = write_cycle.GetFrameSender();
  frame_sender.AddRegularFrame(
      Http2DataFrame{/*stream_id=*/1u, /*end_stream=*/false,
                     SliceBuffer(Slice::FromCopiedString(kData1))});
  frame_sender.AddRegularFrame(
      Http2DataFrame{/*stream_id=*/1u, /*end_stream=*/true, SliceBuffer()});
  frame_sender.AddRegularFrame(Http2SettingsFrame{/*ack=*/false, {}});
  frame_sender.AddRegularFrame(Http2SettingsFrame{/*ack=*/true, {}});
  frame_sender.AddRegularFrame(Http2PingFrame{/*ack=*/false, 1111u});
  frame_sender.AddRegularFrame(Http2PingFrame{/*ack=*/true, 2222u});
  frame_sender.AddUrgentFrame(Http2PingFrame{/*ack=*/false, 3333u});

  // Step 3: Serialize both urgent and regular frames, matching
  // MaybeWriteUrgentFrames and SerializeAndWrite in transport.
  bool reset_ping_clock = false;
  const SliceBuffer urgent_serialized = write_cycle.SerializeUrgentFrames(
      WriteCycle::SerializeStats{reset_ping_clock});
  EXPECT_GT(urgent_serialized.Length(), 0u);

  const SliceBuffer regular_serialized = write_cycle.SerializeRegularFrames(
      WriteCycle::SerializeStats{reset_ping_clock});
  EXPECT_GT(regular_serialized.Length(), 0u);

  // Step 4: Complete the write cycle.
  write_cycle.BeginWrite(regular_serialized.Length());
  write_cycle.EndWrite(/*success=*/true);
  EndWriteCycle();

  // Step 5: Verify all 5 write-cycle metrics in global stats.
  // 1. http2_writes_begun: 1 write cycle started.
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2WritesBegun,
                                 1u);

  // 2. http2_write_target_size: recorded once in the bucket for
  // target_write_size.
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2WriteTargetSize,
      static_cast<int>(target_write_size), 1u);

  // 3. http2_write_data_frame_size: 1 frame in kData1.size() bucket (5 bytes)
  //    and 1 frame in 0-byte bucket (empty END_STREAM DATA frame).
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2WriteDataFrameSize,
      static_cast<int>(kData1.size()), 1u);
  stats_helper.ExpectHistogramBucketCountDiff(
      Http2GlobalStats::Histogram::kHttp2WriteDataFrameSize, 0, 1u);

  // 4. http2_settings_writes: 1 non-ACK SETTINGS frame counted; ACK ignored.
  stats_helper.ExpectCounterDiff(
      Http2GlobalStats::Counter::kHttp2SettingsWrites, 1u);

  // 5. http2_pings_sent: 2 non-ACK PING frames counted (1 regular + 1 urgent);
  //    ACK ignored.
  stats_helper.ExpectCounterDiff(Http2GlobalStats::Counter::kHttp2PingsSent,
                                 2u);
}

INSTANTIATE_TEST_SUITE_P(TransportWriteContextTest, TransportWriteContextTest,
                         ::testing::Bool());

class FrameSenderTest : public TransportWriteContextTest {};

TEST_P(FrameSenderTest, AddRegularFrame) {
  StartWriteCycle();
  WriteCycle& write_cycle = GetWriteCycle();
  FrameSender sender = write_cycle.GetFrameSender();

  EXPECT_EQ(write_cycle.GetRegularFrameCount(), 0u);

  sender.AddRegularFrame(Http2SettingsFrame{});
  EXPECT_EQ(write_cycle.GetRegularFrameCount(), 1u);
}

TEST_P(FrameSenderTest, AddUrgentFrame) {
  StartWriteCycle();
  WriteCycle& write_cycle = GetWriteCycle();
  FrameSender sender = write_cycle.GetFrameSender();

  EXPECT_EQ(write_cycle.GetUrgentFrameCount(), 0u);
  // Urgent frames don't currently affect quota in this implementation.
  uint32_t initial_remaining = write_cycle.GetWriteBytesRemaining();

  sender.AddUrgentFrame(Http2PingFrame{});
  EXPECT_EQ(write_cycle.GetUrgentFrameCount(), 1u);
  EXPECT_EQ(write_cycle.GetWriteBytesRemaining(), initial_remaining);
}

INSTANTIATE_TEST_SUITE_P(FrameSenderTest, FrameSenderTest, ::testing::Bool());

}  // namespace
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
