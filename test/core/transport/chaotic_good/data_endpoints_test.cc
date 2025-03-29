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

#include <google/protobuf/text_format.h>
#include <grpc/grpc.h>

#include <cmath>
#include <cstdint>

#include "gtest/gtest.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

namespace grpc_core {

namespace chaotic_good::data_endpoints_detail {

enum class SendRateOpType : uint8_t {
  kStartSend,
  kMaybeCompleteSend,
  kDeliveryTime
};

struct SendRateOp {
  SendRateOpType type;
  uint64_t current_time;
  uint64_t arg;
};

auto AnySendOp() {
  return fuzztest::Map(
      [](uint8_t type, uint64_t current_time, uint64_t arg) {
        if (current_time == 0) current_time = 1;
        switch (type % 3) {
          case 0:
            return SendRateOp{SendRateOpType::kStartSend, current_time, arg};
          case 1:
            return SendRateOp{SendRateOpType::kMaybeCompleteSend, current_time,
                              0};
          case 2:
            return SendRateOp{SendRateOpType::kDeliveryTime, current_time, arg};
          default:
            LOG(FATAL) << "unreachable";
        }
      },
      fuzztest::Arbitrary<uint8_t>(), fuzztest::Arbitrary<uint64_t>(),
      fuzztest::Arbitrary<uint64_t>());
}

void SendRateIsRobust(double initial_rate, std::vector<SendRateOp> ops) {
  SendRate send_rate(initial_rate);
  for (const auto& op : ops) {
    switch (op.type) {
      case SendRateOpType::kStartSend:
        send_rate.StartSend(op.current_time, op.arg);
        break;
      case SendRateOpType::kMaybeCompleteSend:
        send_rate.MaybeCompleteSend(op.current_time);
        break;
      case SendRateOpType::kDeliveryTime: {
        const double delivery_time =
            send_rate.DeliveryTime(op.current_time, op.arg);
        EXPECT_FALSE(std::isnan(delivery_time));
        EXPECT_GE(delivery_time, 0.0);
        break;
      }
    }
  }
}
FUZZ_TEST(SendRateTest, SendRateIsRobust)
    .WithDomains(fuzztest::InRange<double>(1e-9, 1e9),
                 fuzztest::VectorOf(AnySendOp()));

}  // namespace chaotic_good::data_endpoints_detail

class DataEndpointsTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;
};

chaotic_good::data_endpoints_detail::Clock* Time1Clock() {
  class Clock : public chaotic_good::data_endpoints_detail::Clock {
   public:
    uint64_t Now() override { return 1; }
  };
  static Clock clock;
  return &clock;
}

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
  util::testing::MockPromiseEndpoint ep(1234);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)), event_engine().get(), false,
      Time1Clock());
  ep.ExpectWrite(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello")},
      event_engine().get());
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(123, SliceBuffer(Slice::FromCopiedString("hello"))));
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanMultiWrite) {
  util::testing::MockPromiseEndpoint ep1(1234);
  util::testing::MockPromiseEndpoint ep2(1235);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep1.promise_endpoint),
                std::move(ep2.promise_endpoint)),
      event_engine().get(), false,
      Time1Clock());
  SliceBuffer writes1;
  SliceBuffer writes2;
  ep1.CaptureWrites(writes1, event_engine().get());
  ep2.CaptureWrites(writes2, event_engine().get());
  uint32_t write1_ep = 42;
  uint32_t write2_ep = 42;
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(123, SliceBuffer(Slice::FromCopiedString("hello"))),
      data_endpoints.Write(124, SliceBuffer(Slice::FromCopiedString("world"))));
  TickUntilTrue([&]() { return writes1.Length() + writes2.Length() == 10; });
  WaitForAllPendingWork();
  EXPECT_THAT(write1_ep, ::testing::AnyOf(0, 1));
  EXPECT_THAT(write2_ep, ::testing::AnyOf(0, 1));
  std::string expect[2];
  expect[write1_ep] += "hello";
  expect[write2_ep] += "world";
  LOG(INFO) << GRPC_DUMP_ARGS(write1_ep, write2_ep);
  EXPECT_EQ(writes1.JoinIntoString(), expect[0]);
  EXPECT_EQ(writes2.JoinIntoString(), expect[1]);
}

DATA_ENDPOINTS_TEST(CanRead) {
  util::testing::MockPromiseEndpoint ep(1234);
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)), event_engine().get(), false,
      Time1Clock());
  ep.ExpectRead(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello")},
      event_engine().get());
  SpawnTestSeqWithoutContext("read", data_endpoints.Read(5).Await(),
                             [](absl::StatusOr<SliceBuffer> result) {
                               EXPECT_TRUE(result.ok());
                               EXPECT_EQ(result->JoinIntoString(), "hello");
                             });
  WaitForAllPendingWork();
}

TEST(DataEndpointsTest, CanMultiWriteRegression) {
  CanMultiWrite(ParseTestProto(
      R"pb(event_engine_actions {
             run_delay: 9223372036854775807
             run_delay: 9223372036854775807
             run_delay: 9223372036854775801
             run_delay: 0
             run_delay: 5807413915228537483
             assign_ports: 3508738622
             assign_ports: 4238198998
             assign_ports: 857428670
             assign_ports: 0
             assign_ports: 4227858431
             assign_ports: 2863084513
             assign_ports: 1868867780
             assign_ports: 0
             connections { write_size: 2147483647 write_size: 4294705148 }
             connections { write_size: 1 }
           }
           rng: 1
           rng: 14109448502428080414
           rng: 18446744073709551615
           rng: 13568317980260708783)pb"));
}

}  // namespace grpc_core
