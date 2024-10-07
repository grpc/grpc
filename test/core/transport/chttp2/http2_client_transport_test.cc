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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "test/core/transport/chaotic_good/mock_promise_endpoint.h"
#include "test/core/transport/chaotic_good/transport_test.h"

using grpc_core::chaotic_good::testing::MockPromiseEndpoint;
using grpc_event_engine::experimental::EventEngine;

namespace grpc_core {
namespace http2 {
// Add namespace

TEST(Http2ClientTransportTest, TestHttp2ClientTransportObjectCreation) {
  LOG(INFO) << " Begin TestHttp2ClientTransportObjectCreation";

  MockPromiseEndpoint control_endpoint(1);
  MockPromiseEndpoint data_endpoint(2);
  std::shared_ptr<EventEngine> event_engine =
      grpc_event_engine::experimental::GetDefaultEventEngine();

  auto transport = MakeOrphanable<Http2ClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint),
      CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr),
      event_engine, HPackParser(), HPackCompressor());
  LOG(INFO) << " End TestHttp2ClientTransportObjectCreation";
}

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
