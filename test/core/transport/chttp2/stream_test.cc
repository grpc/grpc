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

#include "src/core/ext/transport/chttp2/transport/stream.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/ref_counted_ptr.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using grpc_event_engine::experimental::EventEngine;

// ============================================================================
// Test Helpers & State Expectations
// ============================================================================

struct StateExpectation {
  bool started;
  bool reads_closed;
  bool half_closed_local;
  bool writes_closed;
};

// Common state expectations for readability
constexpr StateExpectation kIdle = {/*started=*/false, /*reads_closed=*/false,
                                    /*half_closed_local=*/false,
                                    /*writes_closed=*/false};

constexpr StateExpectation kOpen = {/*started=*/true, /*reads_closed=*/false,
                                    /*half_closed_local=*/false,
                                    /*writes_closed=*/false};

constexpr StateExpectation kHalfClosedLocal = {
    /*started=*/true, /*reads_closed=*/false,
    /*half_closed_local=*/true, /*writes_closed=*/false};

constexpr StateExpectation kHalfClosedRemote = {
    /*started=*/true, /*reads_closed=*/true,
    /*half_closed_local=*/false, /*writes_closed=*/false};

constexpr StateExpectation kClosed = {/*started=*/true, /*reads_closed=*/true,
                                      /*half_closed_local=*/true,
                                      /*writes_closed=*/true};

// Asserts the exact expected values of all state queries on a StreamState.
void ExpectState(StateExpectation expected, const StreamState& state) {
  EXPECT_EQ(state.started(), expected.started);
  EXPECT_EQ(state.reads_closed(), expected.reads_closed);
  EXPECT_EQ(state.half_closed_local(), expected.half_closed_local);
  EXPECT_EQ(state.writes_closed(), expected.writes_closed);

  // Derived queries based on the core state booleans
  EXPECT_EQ(state.IsClosed(), expected.reads_closed && expected.writes_closed);
  EXPECT_EQ(state.IsIdle(), !expected.started);
  EXPECT_EQ(state.IsOpen(), expected.started && !expected.reads_closed &&
                                !expected.writes_closed &&
                                !expected.half_closed_local);
  EXPECT_EQ(state.IsHalfClosedLocal(),
            !expected.reads_closed && expected.half_closed_local);
  EXPECT_EQ(state.IsHalfClosedRemote(),
            expected.reads_closed && !expected.writes_closed);
}

// Overloaded helper that asserts the exact expected values on a Stream.
void ExpectState(StateExpectation expected, const Stream& stream) {
  EXPECT_EQ(!stream.IsStreamIdle(), expected.started);
  EXPECT_EQ(stream.IsClosedForReads(), expected.reads_closed);
  EXPECT_EQ(stream.IsHalfClosedLocalSent(), expected.half_closed_local);
  EXPECT_EQ(stream.IsClosedForWrites(), expected.writes_closed);

  // Derived queries based on the core state booleans
  EXPECT_EQ(stream.IsStreamIdle(), !expected.started);
  EXPECT_EQ(stream.IsOpen(), expected.started && !expected.reads_closed &&
                                 !expected.writes_closed &&
                                 !expected.half_closed_local);
  EXPECT_EQ(stream.IsStreamClosed(),
            expected.reads_closed && expected.writes_closed);
  EXPECT_EQ(stream.IsHalfClosedLocal(),
            !expected.reads_closed && expected.half_closed_local);
  EXPECT_EQ(stream.IsStreamHalfClosedRemote(),
            expected.reads_closed && !expected.writes_closed);
}

// Asserts the return value of a state transition.
void ExpectChange(StreamStateChange change, bool reads_became_closed,
                  bool stream_became_closed) {
  EXPECT_EQ(change.reads_became_closed, reads_became_closed);
  EXPECT_EQ(change.stream_became_closed, stream_became_closed);
}

// ============================================================================
// StreamState Client Tests
// ============================================================================

TEST(StreamStateTest, ClientHappyPathOrderA) {
  // Flow: Client starts as idle. It sends initial metadata, then half-closes
  // the stream (finishing the request). Finally, the server sends trailing
  // metadata, which closes the read side and fully closes the stream.
  StreamState state(/*started=*/false);
  ExpectState(kIdle, state);

  // Client sends initial metadata
  state.OnInitialMetadataSent();
  ExpectState(kOpen, state);

  // Client sends half-close (finished sending request)
  ExpectChange(state.OnHalfCloseSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedLocal, state);

  // Client receives trailing metadata from server
  ExpectChange(state.OnTrailingMetadataReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ClientHappyPathOrderB) {
  // Flow: Client starts, sends initial metadata. Server sends trailing metadata
  // before the client has finished sending its request (half-closing). Once
  // the client finally sends its half-close, the stream becomes fully closed.
  StreamState state(/*started=*/false);
  state.OnInitialMetadataSent();

  // Client receives trailing metadata first
  ExpectChange(state.OnTrailingMetadataReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, state);

  // Client sends half-close
  ExpectChange(state.OnHalfCloseSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ClientHappyPathOrderC) {
  // Flow: Client starts, sends initial metadata. It receives trailing metadata
  // from the server (closing the read side). Finally, the client receives a
  // RST_STREAM from the server (e.g. to cleanup/teardown), which fully closes
  // the stream.
  StreamState state(/*started=*/false);
  state.OnInitialMetadataSent();

  // Client receives trailing metadata
  ExpectChange(state.OnTrailingMetadataReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, state);

  // Client receives RST_STREAM
  ExpectChange(state.OnResetReceived(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ClientResetReceived) {
  // Flow: Client starts, sends initial metadata. It then receives a RST_STREAM
  // from the server, which immediately closes the stream for both reads and
  // writes.
  StreamState state(/*started=*/false);
  state.OnInitialMetadataSent();

  ExpectChange(state.OnResetReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ClientResetSent) {
  // Flow: Client starts, sends initial metadata. It then sends a RST_STREAM
  // to the server, which immediately closes the stream for both reads and
  // writes.
  StreamState state(/*started=*/false);
  state.OnInitialMetadataSent();

  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ClientInitiateReset) {
  // Flow: Client starts, sends initial metadata. It decides to initiate a reset
  // locally. This first transitions the read side to closed (OnInitiateReset).
  // Once the RST_STREAM frame is actually written to the wire, the write side
  // also closes, making the stream fully closed (OnResetSent).
  StreamState state(/*started=*/false);
  state.OnInitialMetadataSent();

  // Initiating reset closes reads first
  ExpectChange(state.OnInitiateReset(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, state);

  // Then the transport sends the RST frame
  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

// ============================================================================
// StreamState Server Tests
// ============================================================================

TEST(StreamStateTest, ServerHappyPathOrderA) {
  // Flow: Server starts with started=true. It receives a half-close from the
  // client (closing server reads). It then finishes sending the response and
  // sends trailing metadata, which triggers an internal RST_STREAM kNoError,
  // calling OnResetSent and fully closing the stream.
  StreamState state(/*started=*/true);
  ExpectState(kOpen, state);

  // Server receives half-close from client
  ExpectChange(state.OnHalfCloseReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, state);

  // Server sends response trailers (triggers RST_STREAM kNoError under the
  // hood, calling OnResetSent)
  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ServerHappyPathOrderB) {
  // Flow: Server starts. Before receiving a half-close from the client, the
  // server finishes early and sends trailers (RST_STREAM kNoError /
  // OnResetSent). The stream becomes fully closed immediately.
  StreamState state(/*started=*/true);

  // Server sends trailers / aborts early before client half-closes
  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ServerResetReceived) {
  // Flow: Server starts. It receives a RST_STREAM from the client, which
  // immediately closes the stream for both reads and writes.
  StreamState state(/*started=*/true);

  ExpectChange(state.OnResetReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ServerResetSent) {
  // Flow: Server starts. It sends a RST_STREAM to the client, which
  // immediately closes the stream for both reads and writes.
  StreamState state(/*started=*/true);

  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

TEST(StreamStateTest, ServerInitiateReset) {
  // Flow: Server starts (started=true). It decides to initiate a reset locally
  // (e.g. due to internal error). This first closes reads (OnInitiateReset).
  // Once the RST_STREAM is written to the wire, the stream is fully closed
  // (OnResetSent).
  StreamState state(/*started=*/true);

  // Server initiates reset
  ExpectChange(state.OnInitiateReset(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, state);

  // Server sends RST frame
  ExpectChange(state.OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, state);
}

// ============================================================================
// Stream Integration Tests
// ============================================================================

class StreamIntegrationTest : public ::testing::Test {
 protected:
  StreamIntegrationTest()
      : tfc_("test", /*enable_bdp_probe=*/false, /*memory_owner=*/nullptr),
        arena_([]() {
          auto arena = SimpleArenaAllocator()->MakeArena();
          arena->SetContext<EventEngine>(
              grpc_event_engine::experimental::GetDefaultEventEngine().get());
          return arena;
        }()),
        call_pair_(MakeCallPair(Arena::MakePooledForOverwrite<ClientMetadata>(),
                                arena_)) {}

  ExecCtx exec_ctx_;
  chttp2::TransportFlowControl tfc_;
  RefCountedPtr<Arena> arena_;
  CallInitiatorAndHandler call_pair_;
};

TEST_F(StreamIntegrationTest, ClientHappyPath) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair_.handler.StartCall(), tfc_);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);

  ExpectState(kIdle, *stream);

  // 1. Send initial metadata
  stream->OnInitialMetadataSent();
  ExpectState(kOpen, *stream);

  // 2. Send half close
  ExpectChange(stream->OnHalfCloseSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedLocal, *stream);

  // 3. Receive trailing metadata
  auto server_metadata = Arena::MakePooledForOverwrite<ServerMetadata>();
  ExpectChange(stream->OnTrailingMetadataReceived(std::move(server_metadata)),
               /*reads_became_closed=*/true, /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ServerHappyPath) {
  // Server stream is constructed with CallInitiator
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(std::move(call_pair_.initiator), tfc_,
                             /*stream_id=*/123u,
                             /*allow_true_binary_metadata_peer=*/true);

  ExpectState(kOpen, *stream);

  // 1. Receive half close
  ExpectChange(stream->OnHalfCloseReceived(), /*reads_became_closed=*/true,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, *stream);

  // 2. Send trailing metadata (triggers OnResetSent under the hood)
  ExpectChange(stream->OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
}

TEST_F(StreamIntegrationTest, ClientCancelledBeforeStart) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair_.handler.StartCall(), tfc_);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);

  ExpectState(kIdle, *stream);

  // Mimic local cancellation (or force close due to early error) before start.
  ExpectChange(
      stream->ForceClose(absl::CancelledError("Cancelled before start")),
      /*reads_became_closed=*/true, /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ClientResetReceivedOpen) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair_.handler.StartCall(), tfc_);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);

  ExpectState(kIdle, *stream);

  // 1. Send initial metadata -> Open
  stream->OnInitialMetadataSent();
  ExpectState(kOpen, *stream);

  // 2. Receive RST_STREAM from server -> Closed
  ExpectChange(stream->OnResetReceived(absl::AbortedError("Reset by peer")),
               /*reads_became_closed=*/true, /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ClientResetReceivedHalfClosedLocal) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair_.handler.StartCall(), tfc_);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);

  // 1. Send initial metadata -> Open
  stream->OnInitialMetadataSent();
  // 2. Send half close -> HalfClosedLocal
  ExpectChange(stream->OnHalfCloseSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/false);
  ExpectState(kHalfClosedLocal, *stream);

  // 3. Receive RST_STREAM from server -> Closed
  ExpectChange(stream->OnResetReceived(absl::AbortedError("Reset by peer")),
               /*reads_became_closed=*/true, /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ClientInitiateResetOpen) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair_.handler.StartCall(), tfc_);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);

  // 1. Send initial metadata -> Open
  stream->OnInitialMetadataSent();
  ExpectState(kOpen, *stream);

  // 2. Initiate reset -> HalfClosedRemote (reads closed, writes still open
  // until RST sent)
  ExpectChange(stream->OnInitiateReset(absl::CancelledError("Local cancel")),
               /*reads_became_closed=*/true, /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, *stream);

  // 3. Send RST frame -> Closed
  ExpectChange(stream->OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ServerResetReceivedOpen) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(std::move(call_pair_.initiator), tfc_,
                             /*stream_id=*/123u,
                             /*allow_true_binary_metadata_peer=*/true);
  ExpectState(kOpen, *stream);

  // 1. Receive RST_STREAM from client -> Closed
  ExpectChange(stream->OnResetReceived(absl::AbortedError("Reset by peer")),
               /*reads_became_closed=*/true, /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST_F(StreamIntegrationTest, ServerInitiateResetOpen) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(std::move(call_pair_.initiator), tfc_,
                             /*stream_id=*/123u,
                             /*allow_true_binary_metadata_peer=*/true);
  ExpectState(kOpen, *stream);

  // 1. Initiate reset -> HalfClosedRemote
  ExpectChange(stream->OnInitiateReset(absl::CancelledError("Local cancel")),
               /*reads_became_closed=*/true, /*stream_became_closed=*/false);
  ExpectState(kHalfClosedRemote, *stream);

  // 2. Send RST frame -> Closed
  ExpectChange(stream->OnResetSent(), /*reads_became_closed=*/false,
               /*stream_became_closed=*/true);
  ExpectState(kClosed, *stream);
}

TEST(StreamTest, Minimal) {
  ExecCtx exec_ctx;
  chttp2::TransportFlowControl tfc(/*peer_name=*/"test",
                                   /*enable_bdp_probe=*/false,
                                   /*memory_owner=*/nullptr);
  RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
  arena->SetContext<EventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  CallInitiatorAndHandler call_pair = MakeCallPair(
      Arena::MakePooledForOverwrite<ClientMetadata>(), std::move(arena));
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_pair.handler.StartCall(), tfc);
  stream->InitializeClientStream(
      /*stream_id=*/123u, /*allow_true_binary_metadata_peer=*/true);
  EXPECT_EQ(stream->GetStreamId(), 123u);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
