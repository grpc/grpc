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

#include "src/core/ext/transport/chaotic_good/control_endpoint.h"

#include <grpc/grpc.h>

#include "gtest/gtest.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

namespace grpc_core {

class ControlEndpointTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;
};

#define CONTROL_ENDPOINT_TEST(name) YODEL_TEST(ControlEndpointTest, name)

CONTROL_ENDPOINT_TEST(CanWrite) {
  util::testing::MockPromiseEndpoint ep(1234);
  chaotic_good::ControlEndpoint control_endpoint(std::move(ep.promise_endpoint),
                                                 event_engine().get());
  ep.ExpectWrite(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello")},
      nullptr);
  SpawnTestSeqWithoutContext(
      "write",
      control_endpoint.Write(SliceBuffer(Slice::FromCopiedString("hello"))));
  WaitForAllPendingWork();
}

}  // namespace grpc_core
