//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/http2_client_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/orphanable.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using ::testing::MockFunction;
using ::testing::StrictMock;
using transport::testing::Http2FrameTestHelper;
using util::testing::MockPromiseEndpoint;
using util::testing::TransportTest;

static ClientMetadataHandle TestInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

// Encoded string of header ":path: /demo.Service/Step".
static const std::vector<uint8_t> kPathDemoServiceStep = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x12, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70};

class Http2ClientTransportTest : public TransportTest {
 public:
  Http2ClientTransportTest() {
    grpc_tracer_set_enabled("http2_ph2_transport", true);
  }

 protected:
  Http2FrameTestHelper helper_;
};

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportObjectCreation) {
  // Event Engine      : FuzzingEventEngine
  // This test asserts :
  // 1. Tests Http2ClientTransport object creation and destruction. The object
  // creation itself begins the ReadLoop and the WriteLoop.
  // 2. Assert if the ReadLoop was invoked correctly or not.
  // 3. Tests trivial functions GetTransportName() , server_transport() and
  // client_transport().

  LOG(INFO) << "TestHttp2ClientTransportObjectCreation Begin";
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  mock_endpoint.ExpectRead(
      {helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello!", /*stream_id=*/10, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Bye!", /*stream_id=*/11, /*end_stream=*/true)},
      event_engine().get());

  // Break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());

  EXPECT_EQ(client_transport->filter_stack_transport(), nullptr);
  EXPECT_NE(client_transport->client_transport(), nullptr);
  EXPECT_EQ(client_transport->server_transport(), nullptr);
  EXPECT_EQ(client_transport->GetTransportName(), "http2");

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  LOG(INFO) << "TestHttp2ClientTransportObjectCreation End";
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromQueue) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  auto read = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2DataFrame(
              /*payload=*/"Hello!", /*stream_id=*/10, /*end_stream=*/false),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());

  Http2Frame frame = Http2DataFrame{
      .stream_id = 10,
      .end_stream = false,
      .payload = SliceBuffer(Slice::FromExternalString("Hello!"))};

  auto promise =
      client_transport->TestOnlyEnqueueOutgoingFrame(std::move(frame));
  EXPECT_THAT(promise(), IsReady());
  read();

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromCall) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  SliceBuffer grpc_header;
  std::string data_payload = "Hello!";
  AppendGrpcHeaderToSliceBuffer(grpc_header, 0, data_payload.size());

  // Break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
          kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()))},
      event_engine().get());
  // Expect one data frame and one end of stream frame.
  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/(grpc_header.JoinIntoString() + data_payload),
           /*stream_id=*/1, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"", /*stream_id=*/1, /*end_stream=*/true)},
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());
  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        [initiator]() mutable {
          return initiator.Cancel(absl::CancelledError("Cancelled"));
        },
        []() { return absl::OkStatus(); });
  });
  call.initiator.SpawnInfallible(
      "test-wait", [initator = call.initiator, &on_done]() mutable {
        return Seq(initator.PullServerTrailingMetadata(),
                   [&on_done](ServerMetadataHandle metadata) {
                     on_done.Call();
                     return Empty{};
                   });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
