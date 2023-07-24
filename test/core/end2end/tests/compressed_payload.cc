//
//
// Copyright 2015 gRPC authors.
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

#include <stdint.h>

#include <initializer_list>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

class TestConfigurator {
 public:
  explicit TestConfigurator(CoreEnd2endTest& test) : test_(test) {}

  TestConfigurator& DisableAlgorithmAtServer(
      grpc_compression_algorithm algorithm) {
    server_args_ =
        server_args_.Set(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
                         BitSet<GRPC_COMPRESS_ALGORITHMS_COUNT>()
                             .SetAll(true)
                             .Set(algorithm, false)
                             .ToInt<uint32_t>());
    return *this;
  }

  TestConfigurator& ClientDefaultAlgorithm(
      grpc_compression_algorithm algorithm) {
    client_args_ =
        client_args_.Set(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, algorithm);
    return *this;
  }

  TestConfigurator& ServerDefaultAlgorithm(
      grpc_compression_algorithm algorithm) {
    server_args_ =
        server_args_.Set(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, algorithm);
    return *this;
  }

  TestConfigurator& DecompressInApp() {
    client_args_ =
        client_args_.Set(GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, false);
    server_args_ =
        server_args_.Set(GRPC_ARG_ENABLE_PER_MESSAGE_DECOMPRESSION, false);
    return *this;
  }

  TestConfigurator& ExpectedAlgorithmFromClient(
      grpc_compression_algorithm algorithm) {
    expected_algorithm_from_client_ = algorithm;
    return *this;
  }

  TestConfigurator& ExpectedAlgorithmFromServer(
      grpc_compression_algorithm algorithm) {
    expected_algorithm_from_server_ = algorithm;
    return *this;
  }

  void DisabledAlgorithmTest() {
    Init();
    auto c = test_.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
    auto s = test_.RequestCall(101);
    CoreEnd2endTest::IncomingMetadata server_initial_metadata;
    CoreEnd2endTest::IncomingStatusOnClient server_status;
    c.NewBatch(1)
        .SendInitialMetadata({})
        .SendMessage(std::string(1024, 'x'))
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    test_.Expect(101, true);
    test_.Expect(1, true);
    test_.Step();
    CoreEnd2endTest::IncomingMessage client_message;
    s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
    CoreEnd2endTest::IncomingCloseOnServer client_close;
    test_.Expect(102, false);
    s.NewBatch(103).RecvCloseOnServer(client_close);
    test_.Expect(103, true);
    test_.Step();
    // call was cancelled (closed) ...
    EXPECT_NE(client_close.was_cancelled(), 0);
    // with a certain error
    EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
    // and we expect a specific reason for it
    EXPECT_EQ(server_status.message(),
              "Compression algorithm 'gzip' is disabled.");
    EXPECT_EQ(s.method(), "/foo");
  }

  void RequestWithPayload(
      uint32_t client_send_flags_bitmask,
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          client_init_metadata) {
    Init();
    auto c =
        test_.NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create();
    CoreEnd2endTest::IncomingStatusOnClient server_status;
    CoreEnd2endTest::IncomingMetadata server_initial_metadata;
    c.NewBatch(1)
        .SendInitialMetadata(client_init_metadata)
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    auto s = test_.RequestCall(100);
    test_.Expect(100, true);
    test_.Step();
    EXPECT_TRUE(s.GetEncodingsAcceptedByPeer().all());
    CoreEnd2endTest::IncomingCloseOnServer client_close;
    s.NewBatch(101).SendInitialMetadata({}).RecvCloseOnServer(client_close);
    for (int i = 0; i < 2; i++) {
      c.NewBatch(2).SendMessage(std::string(1024, 'x'),
                                client_send_flags_bitmask);
      test_.Expect(2, true);
      CoreEnd2endTest::IncomingMessage client_message;
      s.NewBatch(102).RecvMessage(client_message);
      test_.Expect(102, true);
      test_.Step();
      EXPECT_EQ(client_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(client_message.payload(), std::string(1024, 'x'));
      EXPECT_EQ(client_message.compression(), expected_algorithm_from_client_);
      s.NewBatch(103).SendMessage(std::string(1024, 'y'));
      CoreEnd2endTest::IncomingMessage server_message;
      c.NewBatch(3).RecvMessage(server_message);
      test_.Expect(103, true);
      test_.Expect(3, true);
      test_.Step();
      EXPECT_EQ(server_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(server_message.payload(), std::string(1024, 'y'));
      EXPECT_EQ(server_message.compression(), expected_algorithm_from_server_);
    }
    c.NewBatch(4).SendCloseFromClient();
    s.NewBatch(104).SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
    test_.Expect(1, true);
    test_.Expect(4, true);
    test_.Expect(101, true);
    test_.Expect(104, true);
    test_.Step();
    EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
    EXPECT_EQ(server_status.message(), "xyz");
    EXPECT_EQ(s.method(), "/foo");
    EXPECT_FALSE(client_close.was_cancelled());
  }

  void RequestWithSendMessageBeforeInitialMetadata() {
    Init();
    auto c =
        test_.NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create();
    c.NewBatch(2).SendMessage(std::string(1024, 'x'));
    test_.Expect(2, true);
    CoreEnd2endTest::IncomingStatusOnClient server_status;
    CoreEnd2endTest::IncomingMetadata server_initial_metadata;
    c.NewBatch(1)
        .SendInitialMetadata({})
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    auto s = test_.RequestCall(100);
    test_.Expect(100, true);
    test_.Step();
    EXPECT_TRUE(s.GetEncodingsAcceptedByPeer().all());
    CoreEnd2endTest::IncomingCloseOnServer client_close;
    s.NewBatch(101).SendInitialMetadata({}).RecvCloseOnServer(client_close);
    for (int i = 0; i < 2; i++) {
      if (i > 0) {
        c.NewBatch(2).SendMessage(std::string(1024, 'x'));
        test_.Expect(2, true);
      }
      CoreEnd2endTest::IncomingMessage client_message;
      s.NewBatch(102).RecvMessage(client_message);
      test_.Expect(102, true);
      test_.Step();
      EXPECT_EQ(client_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(client_message.payload(), std::string(1024, 'x'));
      EXPECT_EQ(client_message.compression(), expected_algorithm_from_client_);
      s.NewBatch(103).SendMessage(std::string(1024, 'y'));
      CoreEnd2endTest::IncomingMessage server_message;
      c.NewBatch(3).RecvMessage(server_message);
      test_.Expect(103, true);
      test_.Expect(3, true);
      test_.Step();
      EXPECT_EQ(server_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(server_message.payload(), std::string(1024, 'y'));
      EXPECT_EQ(server_message.compression(), expected_algorithm_from_server_);
    }
    c.NewBatch(4).SendCloseFromClient();
    s.NewBatch(104).SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
    test_.Expect(1, true);
    test_.Expect(4, true);
    test_.Expect(101, true);
    test_.Expect(104, true);
    test_.Step();
    EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
    EXPECT_EQ(server_status.message(), "xyz");
    EXPECT_EQ(s.method(), "/foo");
    EXPECT_FALSE(client_close.was_cancelled());
  }

  void RequestWithServerLevel(grpc_compression_level server_compression_level) {
    Init();
    auto c = test_.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
    CoreEnd2endTest::IncomingStatusOnClient server_status;
    CoreEnd2endTest::IncomingMetadata server_initial_metadata;
    c.NewBatch(1)
        .SendInitialMetadata({})
        .RecvInitialMetadata(server_initial_metadata)
        .RecvStatusOnClient(server_status);
    auto s = test_.RequestCall(100);
    test_.Expect(100, true);
    test_.Step();
    EXPECT_TRUE(s.GetEncodingsAcceptedByPeer().all());
    CoreEnd2endTest::IncomingCloseOnServer client_close;
    s.NewBatch(101)
        .SendInitialMetadata({}, 0, server_compression_level)
        .RecvCloseOnServer(client_close);
    for (int i = 0; i < 2; i++) {
      c.NewBatch(2).SendMessage(std::string(1024, 'x'));
      test_.Expect(2, true);
      CoreEnd2endTest::IncomingMessage client_message;
      s.NewBatch(102).RecvMessage(client_message);
      test_.Expect(102, true);
      test_.Step();
      EXPECT_EQ(client_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(client_message.payload(), std::string(1024, 'x'));
      EXPECT_EQ(client_message.compression(), expected_algorithm_from_client_);
      s.NewBatch(103).SendMessage(std::string(1024, 'y'));
      CoreEnd2endTest::IncomingMessage server_message;
      c.NewBatch(3).RecvMessage(server_message);
      test_.Expect(103, true);
      test_.Expect(3, true);
      test_.Step();
      EXPECT_EQ(server_message.byte_buffer_type(), GRPC_BB_RAW);
      EXPECT_EQ(server_message.payload(), std::string(1024, 'y'));
      EXPECT_EQ(server_message.compression(), expected_algorithm_from_server_);
    }
    c.NewBatch(4).SendCloseFromClient();
    s.NewBatch(104).SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
    test_.Expect(1, true);
    test_.Expect(4, true);
    test_.Expect(101, true);
    test_.Expect(104, true);
    test_.Step();
    EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
    EXPECT_EQ(server_status.message(), "xyz");
    EXPECT_EQ(s.method(), "/foo");
    EXPECT_FALSE(client_close.was_cancelled());
  }

 private:
  void Init() {
    test_.InitClient(client_args_);
    test_.InitServer(server_args_);
  }

  CoreEnd2endTest& test_;
  ChannelArgs client_args_ = ChannelArgs().Set(
      GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, GRPC_COMPRESS_NONE);
  ChannelArgs server_args_ = ChannelArgs().Set(
      GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, GRPC_COMPRESS_NONE);
  grpc_compression_algorithm expected_algorithm_from_client_ =
      GRPC_COMPRESS_NONE;
  grpc_compression_algorithm expected_algorithm_from_server_ =
      GRPC_COMPRESS_NONE;
};

CORE_END2END_TEST(Http2SingleHopTest, DisabledAlgorithmDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .DisableAlgorithmAtServer(GRPC_COMPRESS_GZIP)
      .DisabledAlgorithmTest();
}

CORE_END2END_TEST(Http2SingleHopTest, DisabledAlgorithmDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .DisableAlgorithmAtServer(GRPC_COMPRESS_GZIP)
      .DecompressInApp()
      .DisabledAlgorithmTest();
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithExceptionallyUncompressedPayloadDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(GRPC_WRITE_NO_COMPRESS, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithExceptionallyUncompressedPayloadDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .DecompressInApp()
      .ExpectedAlgorithmFromServer(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(GRPC_WRITE_NO_COMPRESS, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithUncompressedPayloadDecompressInCore) {
  TestConfigurator(*this).RequestWithPayload(0, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithUncompressedPayloadDecompressInApp) {
  TestConfigurator(*this).DecompressInApp().RequestWithPayload(0, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithCompressedPayloadDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(0, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithCompressedPayloadDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .DecompressInApp()
      .ExpectedAlgorithmFromClient(GRPC_COMPRESS_GZIP)
      .ExpectedAlgorithmFromServer(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(0, {});
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithSendMessageBeforeInitialMetadataDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .RequestWithSendMessageBeforeInitialMetadata();
}

CORE_END2END_TEST(Http2SingleHopTest,
                  RequestWithSendMessageBeforeInitialMetadataDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .ServerDefaultAlgorithm(GRPC_COMPRESS_GZIP)
      .DecompressInApp()
      .ExpectedAlgorithmFromClient(GRPC_COMPRESS_GZIP)
      .ExpectedAlgorithmFromServer(GRPC_COMPRESS_GZIP)
      .RequestWithSendMessageBeforeInitialMetadata();
}

CORE_END2END_TEST(Http2SingleHopTest, RequestWithServerLevelDecompressInCore) {
  TestConfigurator(*this).RequestWithServerLevel(GRPC_COMPRESS_LEVEL_HIGH);
}

CORE_END2END_TEST(Http2SingleHopTest, RequestWithServerLevelDecompressInApp) {
  TestConfigurator(*this)
      .DecompressInApp()
      .ExpectedAlgorithmFromServer(GRPC_COMPRESS_DEFLATE)
      .RequestWithServerLevel(GRPC_COMPRESS_LEVEL_HIGH);
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideNoneToGzipDecompressInCore) {
  TestConfigurator(*this).RequestWithPayload(
      0, {{"grpc-internal-encoding-request", "gzip"}});
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideNoneToGzipDecompressInApp) {
  TestConfigurator(*this)
      .DecompressInApp()
      .ExpectedAlgorithmFromClient(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(0, {{"grpc-internal-encoding-request", "gzip"}});
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideDeflateToGzipDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_DEFLATE)
      .RequestWithPayload(0, {{"grpc-internal-encoding-request", "gzip"}});
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideDeflateToGzipDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_DEFLATE)
      .DecompressInApp()
      .ExpectedAlgorithmFromClient(GRPC_COMPRESS_GZIP)
      .RequestWithPayload(0, {{"grpc-internal-encoding-request", "gzip"}});
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideDeflateToIdentityDecompressInCore) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_DEFLATE)
      .RequestWithPayload(0, {{"grpc-internal-encoding-request", "identity"}});
}

CORE_END2END_TEST(
    Http2SingleHopTest,
    RequestWithCompressedPayloadMetadataOverrideDeflateToIdentityDecompressInApp) {
  TestConfigurator(*this)
      .ClientDefaultAlgorithm(GRPC_COMPRESS_DEFLATE)
      .DecompressInApp()
      .RequestWithPayload(0, {{"grpc-internal-encoding-request", "identity"}});
}

}  // namespace
}  // namespace grpc_core
