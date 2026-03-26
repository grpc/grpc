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
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/write_size_policy.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {
namespace {

constexpr absl::string_view kData1 = "data1";
constexpr absl::string_view kData2 = "data2";
constexpr absl::string_view kData = "data";

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
  WriteBufferTracker tracker(is_first_write, is_client);
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
  WriteBufferTracker tracker(is_first_write, is_client);
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
  WriteBufferTracker tracker(is_first_write, is_client);
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
  WriteBufferTracker tracker(is_first_write, is_client);

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
  WriteBufferTracker tracker(is_first_write, is_client);
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
  for (bool is_client : {false, true}) {
    {
      bool is_first_write = true;
      WriteBufferTracker tracker(is_first_write, is_client);

      tracker.AddRegularFrame(Http2DataFrame{
          1, false, SliceBuffer(Slice::FromCopiedString(kData))});
      bool reset = false;
      // SerializeRegularFrames will set is_first_write to false
      tracker.SerializeRegularFrames({reset});
      EXPECT_FALSE(is_first_write);
    }

    {
      bool is_first_write = true;
      WriteBufferTracker tracker(is_first_write, is_client);

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
  WriteCycle cycle(&policy, is_first_write, is_client);

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
  WriteCycle cycle(&policy, is_first_write, is_client);

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
  WriteCycle cycle(&policy, is_first_write, is_client);

  bool reset = false;
  cycle.SerializeRegularFrames({reset});
  EXPECT_FALSE(is_first_write);
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

  void StartWriteCycle() { transport_write_context_.StartWriteCycle(); }
  void EndWriteCycle() { transport_write_context_.EndWriteCycle(); }

 private:
  TransportWriteContext transport_write_context_;
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
