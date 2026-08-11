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

#include "src/core/ext/transport/chttp2/transport/read_context.h"

#include <optional>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/grpc_check.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace http2 {
namespace testing {

///////////////////////////////////////////////////////////////////////////////
// ReadContextTest

class ReadContextTest : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    mock_endpoint.emplace(1234);
    readContext.emplace(
        /*max_new_streams_per_read_cycle=*/32u, mock_endpoint->promise_endpoint,
        /*is_client=*/GetParam(), GrpcErrors::kMaxSecurityFrameSize,
        /*ping_on_rst_stream_percent=*/0u);
  }

  std::optional<util::testing::MockPromiseEndpoint> mock_endpoint;
  std::optional<ReadContext> readContext;
};

TEST_P(ReadContextTest, InitialState) {
  // Verifies that a newly created readContext is not waiting for continuation
  // frames.
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->max_new_streams_per_read_cycle(), 32);
}

TEST_P(ReadContextTest, MaxNewStreamsZeroIsInvalid) {
  // Verifies that creating a ReadContext with 0 max streams crashes
  // with the expected message.
  EXPECT_DEBUG_DEATH(
      ReadContext(/*max_new_streams_per_read_cycle=*/0,
                  mock_endpoint->promise_endpoint,
                  /*is_client=*/GetParam(), GrpcErrors::kMaxSecurityFrameSize,
                  /*ping_on_rst_stream_percent=*/0),
      "0 is invalid");
}

TEST_P(ReadContextTest, HeaderWithEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 1);
}

TEST_P(ReadContextTest, HeaderWithEndHeadersAndEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true and
  // END_STREAM=true.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_TRUE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 1);
}

TEST_P(ReadContextTest, HeaderWithoutEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/3, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 3);
}

TEST_P(ReadContextTest, HeaderWithoutEndHeadersWithEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false and
  // END_STREAM=true.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/3, /*end_headers=*/false, /*end_stream=*/true);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_TRUE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 3);
}

TEST_P(ReadContextTest, HeaderThenContinuationWithEndHeaders) {
  // Verifies state transition from HEADERS(END_HEADERS=false) to
  // CONTINUATION(END_HEADERS=true).
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/5, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 5);

  Http2ContinuationFrame continuation =
      GenerateContinuationFrame("", /*stream_id=*/5, /*end_headers=*/true);
  readContext->UpdateState(continuation, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, HeaderThenContinuationWithoutEndHeaders) {
  // Verifies state remains in-progress when CONTINUATION has END_HEADERS=false.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/7, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 7);

  Http2ContinuationFrame continuation =
      GenerateContinuationFrame("", /*stream_id=*/7, /*end_headers=*/false);
  readContext->UpdateState(continuation, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, HeaderThenTwoContinuationsWithEndHeadersAtEnd) {
  // Verifies state transition over HEADERS -> CONTINUATION ->
  // CONTINUATION(END_HEADERS=true).
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/9, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 9);

  Http2ContinuationFrame continuation1 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/false);
  readContext->UpdateState(continuation1, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());

  Http2ContinuationFrame continuation2 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/true);
  readContext->UpdateState(continuation2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, NewHeaderFrameAfterContinuationSequence) {
  // Verifies that after a sequence of HEADERS and CONTINUATION frames,
  // processing of a new HEADERS frame resets the readContext state.
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/9, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 9);

  Http2ContinuationFrame continuation1 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/false);
  readContext->UpdateState(continuation1, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());

  Http2ContinuationFrame continuation2 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/true);
  readContext->UpdateState(continuation2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());

  Http2HeaderFrame header2 = GenerateHeaderFrame(
      "", /*stream_id=*/11, /*end_headers=*/true, /*end_stream=*/true);
  readContext->UpdateState(header2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 11);
}

TEST_P(ReadContextTest, DidReceiveDuplicateMetadataChecks) {
  // Scenario 1: Initial metadata frame (end_stream=false)
  Http2HeaderFrame header_initial = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false);
  readContext->UpdateState(header_initial, /*is_existing_stream=*/true);
  // If we haven't pushed initial metadata, it's not a duplicate.
  EXPECT_FALSE(readContext->DidReceiveDuplicateMetadata(
      /*did_receive_initial_metadata=*/false,
      /*did_receive_trailing_metadata=*/false));
  // If we have pushed initial metadata, it's a duplicate.
  EXPECT_TRUE(readContext->DidReceiveDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/false));

  // Scenario 2: Trailing metadata frame (end_stream=true)
  Http2HeaderFrame header_trailing = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true);
  readContext->UpdateState(header_trailing, /*is_existing_stream=*/true);
  // If we haven't pushed trailing metadata, it's not a duplicate.
  EXPECT_FALSE(readContext->DidReceiveDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/false));
  // If we have pushed trailing metadata, it's a duplicate.
  EXPECT_TRUE(readContext->DidReceiveDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/true));
}

TEST(GetPeerStringTest, GetPeerString) {
  util::testing::MockPromiseEndpoint mock_endpoint(1234);
  ReadContext readContext(/*max_new_streams_per_read_cycle=*/32u,
                          mock_endpoint.promise_endpoint,
                          /*is_client=*/true, GrpcErrors::kMaxSecurityFrameSize,
                          /*ping_on_rst_stream_percent=*/0u);
  EXPECT_EQ(readContext.peer_string(),
            Slice::FromCopiedString("ipv4:127.0.0.1:1234"));
}

TEST_P(ReadContextTest, PeerString) {
  EXPECT_EQ(readContext->peer_string(),
            Slice::FromCopiedString("ipv4:127.0.0.1:1234"));
}

TEST(ShouldSendPingOnRstStreamTest, AdequateRangeAndRandomness) {
  // Test 0% rate gives 0% true results.
  const uint32_t kNumTrials = 1000u;
  uint32_t true_count_0 = 0u;
  for (uint32_t i = 0u; i < kNumTrials; ++i) {
    if (ShouldSendPingOnRstStream(0u)) {
      true_count_0++;
    }
  }
  EXPECT_EQ(true_count_0, 0u);

  // Test 100% rate gives 100% true results.
  uint32_t true_count_100 = 0u;
  for (uint32_t i = 0u; i < kNumTrials; ++i) {
    if (ShouldSendPingOnRstStream(100u)) {
      true_count_100++;
    }
  }
  EXPECT_EQ(true_count_100, kNumTrials);

  // Test 50% rate gives approximately 50% true results.
  uint32_t true_count_50 = 0u;
  for (uint32_t i = 0u; i < kNumTrials; ++i) {
    if (ShouldSendPingOnRstStream(50u)) {
      true_count_50++;
    }
  }
  // This is a binomial distribution. Mean is 500.
  // One standard deviations is 15.8.
  // So this range is (200/15.8 = 12.7) standard deviations.
  // The probability of this failing is in the order of (10^-35)
  EXPECT_GE(true_count_50, 300u);
  EXPECT_LE(true_count_50, 700u);
}

TEST_P(ReadContextTest, MaxNewStreamsPausesReadLoop) {
  if (GetParam()) {
    // In gRPC, only a Client can initiate a Stream.
    // Only a gRPC server can receive a new incoming stream.
    // Hence this check is only for Servers.
    return;
  }
  Http2FrameHeader frame_header;
  frame_header.length = 0u;
  frame_header.type = 1u;
  frame_header.flags = 4u;

  // We send one less than the max number of streams per read cycle
  for (uint32_t i = 0u; i < readContext->max_new_streams_per_read_cycle() - 1u;
       ++i) {
    uint32_t stream_id = (i * 2) - 1;
    Http2HeaderFrame header = GenerateHeaderFrame("",
                                                  /*stream_id=*/stream_id,
                                                  /*end_headers=*/true,
                                                  /*end_stream=*/false);
    frame_header.stream_id = stream_id;
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->UpdateState(header, /*is_existing_stream=*/false);
  }
  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame *
      (readContext->max_new_streams_per_read_cycle() - 1u);
  const uint32_t expected_total_frames =
      readContext->max_new_streams_per_read_cycle() - 1u;

  // Verify that it is not paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // This new stream should trigger the pause.
  Http2HeaderFrame final_header = GenerateHeaderFrame(
      "",
      /*stream_id=*/readContext->max_new_streams_per_read_cycle(),
      /*end_headers=*/true,
      /*end_stream=*/false);
  frame_header.stream_id =
      2 * readContext->max_new_streams_per_read_cycle() - 1u;
  readContext->SetCurrentFrameHeader(frame_header);
  readContext->UpdateState(final_header, /*is_existing_stream=*/false);

  // SetPauseReadLoop() resets the counters and sets should_pause to true.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

TEST_P(ReadContextTest, MaxInducedFramesPausesReadLoop) {
  Http2FrameHeader frame_header;
  frame_header.length = 0u;
  frame_header.type = 4u;
  frame_header.flags = 0u;
  frame_header.stream_id = 0u;

  // Pump the counter up to one less than the maximum limit.
  for (uint32_t i = 0u; i < GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;
       ++i) {
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->OnSettingsFrameReceived();
  }
  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame *
      (GrpcErrors::kDefaultMaxPendingInducedFrames - 1u);
  const uint32_t expected_total_frames =
      GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;

  // Verify the read loop is NOT paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Receiving one more induced frame pushes the counter to the limit.
  readContext->SetCurrentFrameHeader(frame_header);
  readContext->OnSettingsFrameReceived();

  // The counts should reset upon entering the pause state.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));  //  Read loop is paused.
}

TEST_P(ReadContextTest, MaxResetStreamsPausesReadLoop) {
  Http2FrameHeader frame_header;
  frame_header.length = 4u;
  frame_header.type = 3u;
  frame_header.flags = 0u;

  // Pump the counter up to one less than the maximum limit.
  for (uint32_t i = 0u; i < kCurrentCycleMaxResetStreams - 1u; ++i) {
    frame_header.stream_id = i;
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->OnResetFrameReceived();
  }

  const uint32_t expected_bytes_per_frame =
      frame_header.length + kFrameHeaderSize;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * (kCurrentCycleMaxResetStreams - 1u);
  const uint32_t expected_total_frames = kCurrentCycleMaxResetStreams - 1u;

  // Verify the read loop is NOT paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Receiving one more RST_STREAM frame pushes the counter to the limit.
  frame_header.stream_id = kCurrentCycleMaxResetStreams;
  readContext->SetCurrentFrameHeader(frame_header);
  readContext->OnResetFrameReceived();

  // Verify the read loop is paused and the counts are reset.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

TEST_P(ReadContextTest, MaxFramesPerReadCyclePausesReadLoop) {
  Http2FrameHeader header;
  header.length = 10u;  // Arbitrary payload length
  header.type = 0u;     // DATA frame
  header.flags = 0u;
  header.stream_id = 1u;

  // Pump the counter up to exactly one frame below the limit.
  for (uint32_t i = 0u; i < kMaxFramesReadPerReadCycle - 1u; ++i) {
    readContext->SetCurrentFrameHeader(header);
  }

  const uint32_t expected_bytes_per_frame = kFrameHeaderSize + header.length;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * (kMaxFramesReadPerReadCycle - 1u);
  const uint32_t expected_total_frames = kMaxFramesReadPerReadCycle - 1u;

  // Verify the byte/frame counters are as expected, and the read loop is not
  // paused.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Sending one more frame hits the limit and triggers SetPauseReadLoop()
  readContext->SetCurrentFrameHeader(header);

  // The loop is paused, and counts are reset.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

TEST_P(ReadContextTest, OnResetFrameEnqueuedIncrementsInducedFrames) {
  // 1. error_code == 0 should NOT increment the induced frames counter.
  for (uint32_t i = 0u; i < GrpcErrors::kDefaultMaxPendingInducedFrames + 10u;
       ++i) {
    readContext->OnResetFrameEnqueued(0u);
  }

  // Even though we enqueued more than the limit, the loop is not paused.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/false));

  // 2. error_code != 0 should increment the counter.
  // Pump the counter up to one less than the maximum limit.
  Http2FrameHeader frame_header;
  frame_header.length = 0u;
  frame_header.type = 4u;
  frame_header.flags = 0u;
  frame_header.stream_id = 0u;
  for (uint32_t i = 0u; i < GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;
       ++i) {
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->OnResetFrameEnqueued(1u);
  }
  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame *
      (GrpcErrors::kDefaultMaxPendingInducedFrames - 1u);
  const uint32_t expected_total_frames =
      GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;

  // Verify the read loop is NOT paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Enqueuing one more RST_STREAM frame pushes the counter to the limit and
  // triggers SetPauseReadLoop().
  readContext->OnResetFrameEnqueued(1u);

  // The limit is hit, the loop should pause and counts are reset.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

// If we try to test the increment of induced frames by testing the should_pause
// flag, we will hit the max reset frames in one cycle limit first.
// Hence checking the should_send_ping_on_rst_stream flag which proves the
// induced frames counter was incremented is a better way to test this.
TEST_P(ReadContextTest, ResetFrameTriggersPingBasedOnPercent) {
  // Ping on RST_STREAM DoS mitigation is only supported in servers.
  if (GetParam()) return;

  // Scenario 1: 100% chance to ping
  ReadContext context_100(
      /*max_new_streams_per_read_cycle=*/32u, mock_endpoint->promise_endpoint,
      /*is_client=*/false, GrpcErrors::kMaxSecurityFrameSize,
      /*ping_on_rst_stream_percent=*/100u);

  // It MUST return true, meaning the ping is triggered and Induced Frames
  // increased
  EXPECT_TRUE(
      context_100.OnResetFrameReceived().should_send_ping_on_rst_stream);

  // Scenario 2: 0% chance to ping
  ReadContext context_0(
      /*max_new_streams_per_read_cycle=*/32u, mock_endpoint->promise_endpoint,
      /*is_client=*/false, GrpcErrors::kMaxSecurityFrameSize,
      /*ping_on_rst_stream_percent=*/0u);

  // It MUST return false, meaning no ping is triggered and Induced Frames did
  // NOT increase
  EXPECT_FALSE(context_0.OnResetFrameReceived().should_send_ping_on_rst_stream);
}

INSTANTIATE_TEST_SUITE_P(ReadContext, ReadContextTest, ::testing::Bool());

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
