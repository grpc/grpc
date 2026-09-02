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
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
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
  EXPECT_EQ(readContext->max_new_streams_per_read_cycle(), 32u);
}

TEST_P(ReadContextTest, MaxNewStreamsZeroIsInvalid) {
  // Verifies that creating a ReadContext with 0 max streams crashes
  // with the expected message.
  EXPECT_DEBUG_DEATH(
      ReadContext(/*max_new_streams_per_read_cycle=*/0u,
                  mock_endpoint->promise_endpoint,
                  /*is_client=*/GetParam(), GrpcErrors::kMaxSecurityFrameSize,
                  /*ping_on_rst_stream_percent=*/0u),
      "0 is invalid");
}

TEST_P(ReadContextTest, HeaderWithEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/1u, /*end_headers=*/true, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 1u);
}

TEST_P(ReadContextTest, HeaderWithEndHeadersAndEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true and
  // END_STREAM=true.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/1u, /*end_headers=*/true, /*end_stream=*/true);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_TRUE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 1u);
}

TEST_P(ReadContextTest, HeaderWithoutEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/3u, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 3u);
}

TEST_P(ReadContextTest, HeaderWithoutEndHeadersWithEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false and
  // END_STREAM=true.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/3u, /*end_headers=*/false, /*end_stream=*/true);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_TRUE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 3u);
}

TEST_P(ReadContextTest, HeaderThenContinuationWithEndHeaders) {
  // Verifies state transition from HEADERS(END_HEADERS=false) to
  // CONTINUATION(END_HEADERS=true).
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/5u, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_FALSE(readContext->HeaderHasEndStream());
  EXPECT_EQ(readContext->GetStreamId(), 5u);

  const Http2ContinuationFrame continuation_frame =
      GenerateContinuationFrame("", /*stream_id=*/5u, /*end_headers=*/true);
  readContext->UpdateState(continuation_frame, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, HeaderThenContinuationWithoutEndHeaders) {
  // Verifies state remains in-progress when CONTINUATION has END_HEADERS=false.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/7u, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 7u);

  const Http2ContinuationFrame continuation_frame =
      GenerateContinuationFrame("", /*stream_id=*/7u, /*end_headers=*/false);
  readContext->UpdateState(continuation_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, HeaderThenTwoContinuationsWithEndHeadersAtEnd) {
  // Verifies state transition over HEADERS -> CONTINUATION ->
  // CONTINUATION(END_HEADERS=true).
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/9u, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 9u);

  const Http2ContinuationFrame continuation_frame1 =
      GenerateContinuationFrame("", /*stream_id=*/9u, /*end_headers=*/false);
  readContext->UpdateState(continuation_frame1, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());

  const Http2ContinuationFrame continuation_frame2 =
      GenerateContinuationFrame("", /*stream_id=*/9u, /*end_headers=*/true);
  readContext->UpdateState(continuation_frame2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
}

TEST_P(ReadContextTest, NewHeaderFrameAfterContinuationSequence) {
  // Verifies that after a sequence of HEADERS and CONTINUATION frames,
  // processing of a new HEADERS frame resets the readContext state.
  const Http2HeaderFrame header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/9u, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(header_frame, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 9u);

  const Http2ContinuationFrame continuation_frame1 =
      GenerateContinuationFrame("", /*stream_id=*/9u, /*end_headers=*/false);
  readContext->UpdateState(continuation_frame1, /*is_existing_stream=*/true);
  EXPECT_TRUE(readContext->IsWaitingForContinuationFrame());

  const Http2ContinuationFrame continuation_frame2 =
      GenerateContinuationFrame("", /*stream_id=*/9u, /*end_headers=*/true);
  readContext->UpdateState(continuation_frame2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());

  const Http2HeaderFrame header_frame2 = GenerateHeaderFrame(
      "", /*stream_id=*/11u, /*end_headers=*/true, /*end_stream=*/true);
  readContext->UpdateState(header_frame2, /*is_existing_stream=*/true);
  EXPECT_FALSE(readContext->IsWaitingForContinuationFrame());
  EXPECT_EQ(readContext->GetStreamId(), 11u);
}

TEST_P(ReadContextTest, DidReceiveDuplicateMetadataChecks) {
  // Scenario 1: Initial metadata frame (end_stream=false)
  const Http2HeaderFrame header_initial = GenerateHeaderFrame(
      "", /*stream_id=*/1u, /*end_headers=*/true, /*end_stream=*/false);
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
  const Http2HeaderFrame header_trailing = GenerateHeaderFrame(
      "", /*stream_id=*/1u, /*end_headers=*/true, /*end_stream=*/true);
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
  frame_header.type = 1u;  // HEADERS frame
  frame_header.flags = 4u;

  // We send one less than the max number of streams per read cycle
  // Streams initiated by a client MUST use odd-numbered stream identifiers.
  for (uint32_t i = 1u; i < readContext->max_new_streams_per_read_cycle();
       ++i) {
    const uint32_t stream_id = (i * 2u) - 1u;
    const Http2HeaderFrame header_frame =
        GenerateHeaderFrame("",
                            /*stream_id=*/stream_id,
                            /*end_headers=*/true,
                            /*end_stream=*/false);
    frame_header.stream_id = stream_id;
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->UpdateState(header_frame, /*is_existing_stream=*/false);
  }
  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_frames =
      readContext->max_new_streams_per_read_cycle() - 1u;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * expected_total_frames;

  // Verify that it is not paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // This new stream should trigger the pause.
  const uint32_t final_stream_id =
      (2u * readContext->max_new_streams_per_read_cycle()) - 1u;
  const Http2HeaderFrame final_header_frame =
      GenerateHeaderFrame("",
                          /*stream_id=*/final_stream_id,
                          /*end_headers=*/true,
                          /*end_stream=*/false);
  frame_header.stream_id = final_stream_id;
  readContext->SetCurrentFrameHeader(frame_header);
  readContext->UpdateState(final_header_frame, /*is_existing_stream=*/false);

  // SetPauseReadLoop() resets the counters and sets should_pause to true.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

TEST_P(ReadContextTest, MaxInducedFramesPausesReadLoop) {
  Http2FrameHeader frame_header;
  frame_header.length = 0u;
  // OnSettingsFrameReceived() increments induced frames independently of frame
  // type. Solely used to populate read counters for reset verification.
  frame_header.type = 4u;  // Settings frame
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
  const uint32_t expected_total_frames =
      GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * expected_total_frames;

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
  frame_header.type = 3u;  // RST_STREAM frame
  frame_header.flags = 0u;

  // Pump the counter up to one less than the maximum limit.
  // Streams initiated by a client MUST use odd-numbered stream identifiers.
  for (uint32_t i = 1u; i < kCurrentCycleMaxResetStreams; ++i) {
    frame_header.stream_id = (i * 2u) - 1u;
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->OnResetFrameReceived();
  }

  const uint32_t expected_bytes_per_frame =
      frame_header.length + kFrameHeaderSize;
  const uint32_t expected_total_frames = kCurrentCycleMaxResetStreams - 1u;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * expected_total_frames;

  // Verify the read loop is NOT paused yet along with the counters.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Receiving one more RST_STREAM frame pushes the counter to the limit.
  frame_header.stream_id = (2u * kCurrentCycleMaxResetStreams) - 1u;
  readContext->SetCurrentFrameHeader(frame_header);
  readContext->OnResetFrameReceived();

  // Verify the read loop is paused and the counts are reset.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/0u,
      /*expected_read_count=*/0u,
      /*should_pause=*/true));
}

TEST_P(ReadContextTest, MaxFramesPerReadCyclePausesReadLoop) {
  Http2FrameHeader frame_header;
  frame_header.length = 10u;  // Arbitrary payload length
  frame_header.type = 0u;     // DATA frame
  frame_header.flags = 0u;
  frame_header.stream_id = 1u;

  // Pump the counter up to exactly one frame below the limit.
  for (uint32_t i = 0u; i < kMaxFramesReadPerReadCycle - 1u; ++i) {
    readContext->SetCurrentFrameHeader(frame_header);
  }

  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_frames = kMaxFramesReadPerReadCycle - 1u;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * expected_total_frames;

  // Verify the byte/frame counters are as expected, and the read loop is not
  // paused.
  EXPECT_TRUE(readContext->TestOnlyCheckCounters(
      /*expected_bytes_read=*/expected_total_bytes,
      /*expected_read_count=*/expected_total_frames,
      /*should_pause=*/false));

  // Sending one more frame hits the limit and triggers SetPauseReadLoop()
  readContext->SetCurrentFrameHeader(frame_header);

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
  frame_header.type = 4u;  // Settings frame
  frame_header.flags = 0u;
  frame_header.stream_id = 0u;
  for (uint32_t i = 0u; i < GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;
       ++i) {
    readContext->SetCurrentFrameHeader(frame_header);
    readContext->OnResetFrameEnqueued(1u);
  }
  const uint32_t expected_bytes_per_frame =
      kFrameHeaderSize + frame_header.length;
  const uint32_t expected_total_frames =
      GrpcErrors::kDefaultMaxPendingInducedFrames - 1u;
  const uint32_t expected_total_bytes =
      expected_bytes_per_frame * expected_total_frames;

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

  Http2FrameHeader frame_header;
  frame_header.length = 0u;
  frame_header.type = 3u;  // RST_STREAM frame
  frame_header.flags = 0u;
  frame_header.stream_id = 1u;
  context_100.SetCurrentFrameHeader(frame_header);

  // It MUST return true, meaning the ping is triggered and Induced Frames
  // increased
  EXPECT_TRUE(
      context_100.OnResetFrameReceived().should_send_ping_on_rst_stream);

  // Scenario 2: 0% chance to ping
  ReadContext context_0(
      /*max_new_streams_per_read_cycle=*/32u, mock_endpoint->promise_endpoint,
      /*is_client=*/false, GrpcErrors::kMaxSecurityFrameSize,
      /*ping_on_rst_stream_percent=*/0u);

  context_0.SetCurrentFrameHeader(frame_header);

  // It MUST return false, meaning no ping is triggered and Induced Frames did
  // NOT increase
  EXPECT_FALSE(context_0.OnResetFrameReceived().should_send_ping_on_rst_stream);
}

// When corrupted buffered fragments are encountered, a connection error is
// returned, overriding the original status.
TEST_P(ReadContextTest, ParseAndDiscardHeadersCorruptedBufferedFragments) {
  // Generate a garbage header frame.
  // We set end_headers = false so the assembler keeps it buffered
  // and waits for a continuation frame.
  Http2HeaderFrame bad_header_frame =
      GenerateHeaderFrame("invalid_garbage_hpack_bytes", /*stream_id=*/5u,
                          /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(bad_header_frame, /*is_existing_stream=*/true);

  // Inject the garbage into the HeaderAssembler's internal state.
  // We set the stream ID first to satisfy the internal GRPC_DCHECK.
  readContext->header_assembler().SetStreamId(5u);
  Http2Status append_status =
      readContext->header_assembler().AppendFrame(bad_header_frame);

  EXPECT_TRUE(append_status.IsOk());

  // Append a dummy continuation frame with end_headers = true to mark the
  // buffered fragments sequence complete.
  Http2ContinuationFrame continuation_frame =
      GenerateContinuationFrame("", 5u, /*end_headers=*/true);
  const Http2Status continuation_status =
      readContext->header_assembler().AppendFrame(continuation_frame);
  EXPECT_TRUE(continuation_status.IsOk());

  // By passing an empty buffer and is_end_headers = true
  SliceBuffer empty_buffer;
  Http2Status original_status = Http2Status::Ok();

  // Header Assembler will try to parse the corrupted buffered fragments first.
  Http2Status result = readContext->ParseAndDiscardHeaders(
      std::move(empty_buffer),
      /*is_end_headers=*/true, std::move(original_status),
      /*max_header_list_size_hard_limit=*/1024);

  // The result should be a connection error.
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetType(), Http2Status::Http2ErrorType::kConnectionError);
}

TEST_P(ReadContextTest, ParseAndDiscardHeadersValidData) {
  SliceBuffer valid_hpack_buffer;
  // 0x82 is the HPACK indexed representation for ":method: GET"
  valid_hpack_buffer.Append(Slice::FromCopiedString("\x82"));

  Http2Status original_status = Http2Status::Ok();

  Http2Status result = readContext->ParseAndDiscardHeaders(
      std::move(valid_hpack_buffer),
      /*is_end_headers=*/true, std::move(original_status),
      /*max_header_list_size_hard_limit=*/1024);

  // It should parse successfully and return our original OK status.
  EXPECT_TRUE(result.IsOk());
}

TEST_P(ReadContextTest, ParseAndDiscardHeadersEmptyBuffer) {
  SliceBuffer empty_buffer;
  Http2Status original_status = Http2Status::Ok();

  Http2Status result = readContext->ParseAndDiscardHeaders(
      std::move(empty_buffer),
      /*is_end_headers=*/true, std::move(original_status),
      /*max_header_list_size_hard_limit=*/1024);

  // It should bypass parsing entirely and return the original OK status.
  EXPECT_TRUE(result.IsOk());
}

// Garbage HPACK data gives connection error.
TEST_P(ReadContextTest, ParseAndDiscardHeadersInvalidData) {
  SliceBuffer bad_buffer;
  bad_buffer.Append(Slice::FromCopiedString("invalid_garbage_hpack_bytes"));

  Http2Status original_status = Http2Status::Http2StreamError(
      Http2ErrorCode::kCancel, "Stream cancelled");

  Http2Status result = readContext->ParseAndDiscardHeaders(
      std::move(bad_buffer),
      /*is_end_headers=*/true, std::move(original_status),
      /*max_header_list_size_hard_limit=*/1024);

  // The parser failure MUST escalate to a fatal Connection Error,
  // overriding whatever the original_status was.
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetType(), Http2Status::Http2ErrorType::kConnectionError);
}

TEST_P(ReadContextTest, ValidateHeaderSuccess) {
  Http2FrameHeader frame_header;
  frame_header.length = 100;
  frame_header.type = 1;  // HEADERS frame
  frame_header.flags = 0;
  frame_header.stream_id = 5;

  // Clients only receive frames on existing streams (<= last_stream_id),
  // whereas servers can accept new stream initiations (> last_stream_id) only
  // for HEADERS. Non-HEADERS (e.g., DATA) on an idle stream (> last_stream_id)
  // are protocol errors.
  uint32_t last_stream_id = GetParam() ? 5 : 0;

  // This frame is within limits and has a valid stream ID.
  Http2Status result = readContext->ValidateHeader(
      /*max_frame_size_setting=*/16384, frame_header,
      /*last_stream_id=*/last_stream_id,
      /*is_first_settings_processed=*/true);

  EXPECT_TRUE(result.IsOk());
}

TEST_P(ReadContextTest, ValidateHeaderOversizedFrame) {
  Http2FrameHeader frame_header;
  frame_header.length = 20000;
  frame_header.type = 0;  // DATA frame
  frame_header.flags = 0;
  frame_header.stream_id = 5;

  Http2Status result = readContext->ValidateHeader(
      /*max_frame_size_setting=*/16384, frame_header,
      /*last_stream_id=*/5u,
      /*is_first_settings_processed=*/true);

  // The validator should reject this with a Frame Size Error
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kFrameSizeError);
}

TEST_P(ReadContextTest, ValidateHeaderExpectingContinuation) {
  // Put the ReadContext into a state where it is expecting
  // a CONTINUATION frame i.e. end_headers = false.
  Http2HeaderFrame initial_header_frame = GenerateHeaderFrame(
      "", /*stream_id=*/5, /*end_headers=*/false, /*end_stream=*/false);
  readContext->UpdateState(initial_header_frame, /*is_existing_stream=*/false);

  // Now try to validate a NEW HEADERS frame on stream 7.
  Http2FrameHeader new_header;
  new_header.length = 100;
  new_header.type = 1;  // HEADERS frame
  new_header.flags = 0;
  new_header.stream_id = 7;

  Http2Status result = readContext->ValidateHeader(
      /*max_frame_size_setting=*/16384, new_header,
      /*last_stream_id=*/GetParam() ? 7 : 0,
      /*is_first_settings_processed=*/true);

  // It MUST fail because we are in the middle of a header block for stream 5.
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

TEST_P(ReadContextTest, ValidateHeaderUnexpectedContinuation) {
  Http2FrameHeader header;
  header.length = 100;
  header.type = 9;  // CONTINUATION frame
  header.flags = 0;
  header.stream_id = 5;

  // Notice we did NOT call UpdateState() first.
  // ReadContext is NOT expecting a continuation frame.
  Http2Status result = readContext->ValidateHeader(
      /*max_frame_size_setting=*/16384, header,
      /*last_stream_id=*/GetParam() ? 5 : 0,
      /*is_first_settings_processed=*/true);

  // The validator must reject this because no header block was started.
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

TEST_P(ReadContextTest, ValidateHeaderContinuationForWrongStream) {
  // Put the context into a state expecting a CONTINUATION for Stream 5.
  Http2HeaderFrame initial_header_frame =
      GenerateHeaderFrame("",
                          /*stream_id=*/5,
                          /*end_headers=*/false,
                          /*end_stream=*/false);
  readContext->UpdateState(initial_header_frame, /*is_existing_stream=*/false);

  // The peer now sends a CONTINUATION frame, but for Stream 7.
  Http2FrameHeader new_header;
  new_header.length = 100;
  new_header.type = 9;  // CONTINUATION frame
  new_header.flags = 0;
  new_header.stream_id = 7;

  Http2Status result = readContext->ValidateHeader(
      /*max_frame_size_setting=*/16384, new_header,
      /*last_stream_id=*/GetParam() ? 7 : 0,
      /*is_first_settings_processed=*/true);

  // The validator must reject this because the stream IDs don't match.
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

INSTANTIATE_TEST_SUITE_P(ReadContext, ReadContextTest, ::testing::Bool());

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
