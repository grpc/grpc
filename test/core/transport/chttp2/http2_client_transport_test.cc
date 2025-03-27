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
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/util/orphanable.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using transport::testing::Http2FrameTestHelper;
using util::testing::MockPromiseEndpoint;
using util::testing::TransportTest;

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

  auto promise = client_transport->EnqueueOutgoingFrame(std::move(frame))();
  EXPECT_THAT(promise(), IsReady());
  read();

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
