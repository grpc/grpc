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

#include "src/core/ext/transport/chaotic_good/data_endpoints.h"

#include <grpc/grpc.h>

#include "gtest/gtest.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

namespace grpc_core {

class DataEndpointsTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;
};

#define DATA_ENDPOINTS_TEST(name) YODEL_TEST(DataEndpointsTest, name)

template <typename... Args>
std::vector<chaotic_good::PendingConnection> Endpoints(Args... args) {
  std::vector<chaotic_good::PendingConnection> connections;
  std::vector<int> this_is_just_here_to_get_the_statements_to_unpack = {
      (connections.emplace_back(
           chaotic_good::ImmediateConnection("foo", std::move(args))),
       0)...};
  return connections;
}

DATA_ENDPOINTS_TEST(CanWrite) {
  chaotic_good::testing::MockPromiseEndpoint ep(1234);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)), event_engine().get(), false);
  ep.ExpectWrite(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello")},
      event_engine().get());
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(SliceBuffer(Slice::FromCopiedString("hello"))),
      [](uint32_t id) { EXPECT_EQ(id, 0); });
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanMultiWrite) {
  chaotic_good::testing::MockPromiseEndpoint ep1(1234);
  chaotic_good::testing::MockPromiseEndpoint ep2(1235);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep1.promise_endpoint),
                std::move(ep2.promise_endpoint)),
      event_engine().get(), false);
  SliceBuffer writes1;
  SliceBuffer writes2;
  ep1.CaptureWrites(writes1, event_engine().get());
  ep2.CaptureWrites(writes2, event_engine().get());
  uint32_t write1_ep = 42;
  uint32_t write2_ep = 42;
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(SliceBuffer(Slice::FromCopiedString("hello"))),
      [&write1_ep](uint32_t id) { write1_ep = id; },
      data_endpoints.Write(SliceBuffer(Slice::FromCopiedString("world"))),
      [&write2_ep](uint32_t id) { write2_ep = id; });
  WaitForAllPendingWork();
  EXPECT_THAT(write1_ep, ::testing::AnyOf(0, 1));
  EXPECT_THAT(write2_ep, ::testing::AnyOf(0, 1));
  std::string expect[2];
  expect[write1_ep] += "hello";
  expect[write2_ep] += "world";
  EXPECT_EQ(writes1.JoinIntoString(), expect[0]);
  EXPECT_EQ(writes2.JoinIntoString(), expect[1]);
}

DATA_ENDPOINTS_TEST(CanRead) {
  chaotic_good::testing::MockPromiseEndpoint ep(1234);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)), event_engine().get(), false);
  ep.ExpectRead(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello")},
      event_engine().get());
  SpawnTestSeqWithoutContext("read", data_endpoints.Read(0, 5).Await(),
                             [](absl::StatusOr<SliceBuffer> result) {
                               EXPECT_TRUE(result.ok());
                               EXPECT_EQ(result->JoinIntoString(), "hello");
                             });
  WaitForAllPendingWork();
}

}  // namespace grpc_core
