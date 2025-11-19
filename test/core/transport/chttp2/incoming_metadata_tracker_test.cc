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

#include "src/core/ext/transport/chttp2/transport/incoming_metadata_tracker.h"

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/util/grpc_check.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace http2 {
namespace testing {

///////////////////////////////////////////////////////////////////////////////
// IncomingMetadataTrackerTest

TEST(IncomingMetadataTrackerTest, InitialState) {
  // Verifies that a newly created tracker is not waiting for continuation
  // frames.
  IncomingMetadataTracker tracker;
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
}

TEST(IncomingMetadataTrackerTest, HeaderWithEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
  EXPECT_FALSE(tracker.HeaderHasEndStream());
  EXPECT_EQ(tracker.GetStreamId(), 1);
}

TEST(IncomingMetadataTrackerTest, HeaderWithEndHeadersAndEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=true and
  // END_STREAM=true.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true);
  tracker.OnHeaderReceived(header);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
  EXPECT_TRUE(tracker.HeaderHasEndStream());
  EXPECT_EQ(tracker.GetStreamId(), 1);
}

TEST(IncomingMetadataTrackerTest, HeaderWithoutEndHeaders) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/3, /*end_headers=*/false, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_FALSE(tracker.HeaderHasEndStream());
  EXPECT_EQ(tracker.GetStreamId(), 3);
}

TEST(IncomingMetadataTrackerTest, HeaderWithoutEndHeadersWithEndStream) {
  // Verifies state after receiving a HEADERS frame with END_HEADERS=false and
  // END_STREAM=true.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/3, /*end_headers=*/false, /*end_stream=*/true);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_TRUE(tracker.HeaderHasEndStream());
  EXPECT_EQ(tracker.GetStreamId(), 3);
}

TEST(IncomingMetadataTrackerTest, HeaderThenContinuationWithEndHeaders) {
  // Verifies state transition from HEADERS(END_HEADERS=false) to
  // CONTINUATION(END_HEADERS=true).
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/5, /*end_headers=*/false, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_FALSE(tracker.HeaderHasEndStream());
  EXPECT_EQ(tracker.GetStreamId(), 5);

  Http2ContinuationFrame continuation =
      GenerateContinuationFrame("", /*stream_id=*/5, /*end_headers=*/true);
  tracker.OnContinuationReceived(continuation);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
}

TEST(IncomingMetadataTrackerTest, HeaderThenContinuationWithoutEndHeaders) {
  // Verifies state remains in-progress when CONTINUATION has END_HEADERS=false.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/7, /*end_headers=*/false, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_EQ(tracker.GetStreamId(), 7);

  Http2ContinuationFrame continuation =
      GenerateContinuationFrame("", /*stream_id=*/7, /*end_headers=*/false);
  tracker.OnContinuationReceived(continuation);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
}

TEST(IncomingMetadataTrackerTest,
     HeaderThenTwoContinuationsWithEndHeadersAtEnd) {
  // Verifies state transition over HEADERS -> CONTINUATION ->
  // CONTINUATION(END_HEADERS=true).
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/9, /*end_headers=*/false, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_EQ(tracker.GetStreamId(), 9);

  Http2ContinuationFrame continuation1 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/false);
  tracker.OnContinuationReceived(continuation1);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());

  Http2ContinuationFrame continuation2 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/true);
  tracker.OnContinuationReceived(continuation2);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
}

TEST(IncomingMetadataTrackerTest, NewHeaderFrameAfterContinuationSequence) {
  // Verifies that after a sequence of HEADERS and CONTINUATION frames,
  // processing of a new HEADERS frame resets the tracker state.
  IncomingMetadataTracker tracker;
  Http2HeaderFrame header = GenerateHeaderFrame(
      "", /*stream_id=*/9, /*end_headers=*/false, /*end_stream=*/false);
  tracker.OnHeaderReceived(header);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());
  EXPECT_EQ(tracker.GetStreamId(), 9);

  Http2ContinuationFrame continuation1 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/false);
  tracker.OnContinuationReceived(continuation1);
  EXPECT_TRUE(tracker.IsWaitingForContinuationFrame());

  Http2ContinuationFrame continuation2 =
      GenerateContinuationFrame("", /*stream_id=*/9, /*end_headers=*/true);
  tracker.OnContinuationReceived(continuation2);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());

  Http2HeaderFrame header2 = GenerateHeaderFrame(
      "", /*stream_id=*/11, /*end_headers=*/true, /*end_stream=*/true);
  tracker.OnHeaderReceived(header2);
  EXPECT_FALSE(tracker.IsWaitingForContinuationFrame());
  EXPECT_EQ(tracker.GetStreamId(), 11);
}

TEST(IncomingMetadataTrackerTest, ClientReceivedDuplicateMetadataChecks) {
  // Verifies ClientReceivedDuplicateMetadata logic.
  IncomingMetadataTracker tracker;

  // Scenario 1: Initial metadata frame (end_stream=false)
  Http2HeaderFrame header_initial = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false);
  tracker.OnHeaderReceived(header_initial);
  // If we haven't pushed initial metadata, it's not a duplicate.
  EXPECT_FALSE(tracker.ClientReceivedDuplicateMetadata(
      /*did_receive_initial_metadata=*/false,
      /*did_receive_trailing_metadata=*/false));
  // If we have pushed initial metadata, it's a duplicate.
  EXPECT_TRUE(tracker.ClientReceivedDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/false));

  // Scenario 2: Trailing metadata frame (end_stream=true)
  Http2HeaderFrame header_trailing = GenerateHeaderFrame(
      "", /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true);
  tracker.OnHeaderReceived(header_trailing);
  // If we haven't pushed trailing metadata, it's not a duplicate.
  EXPECT_FALSE(tracker.ClientReceivedDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/false));
  // If we have pushed trailing metadata, it's a duplicate.
  EXPECT_TRUE(tracker.ClientReceivedDuplicateMetadata(
      /*did_receive_initial_metadata=*/true,
      /*did_receive_trailing_metadata=*/true));
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
