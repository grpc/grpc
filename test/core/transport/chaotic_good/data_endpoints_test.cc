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

#include "gmock/gmock.h"
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
            send_rate.GetLbDecision(op.current_time, op.arg)
                .delivery_time.value();
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

TEST(DataFrameHeaderTest, CanSerialize) {
  TcpDataFrameHeader header;
  header.payload_tag = 0x0012'3456'789a'bcde;
  header.send_timestamp = 0x1234'5678'9abc'def0;
  header.payload_length = 0x1234'5678;
  uint8_t buffer[TcpDataFrameHeader::kFrameHeaderSize];
  header.Serialize(buffer);
  uint8_t expect[TcpDataFrameHeader::kFrameHeaderSize] = {
      0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x00, 0xf0, 0xde,
      0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
  EXPECT_EQ(std::string(buffer, buffer + TcpDataFrameHeader::kFrameHeaderSize),
            std::string(expect, expect + TcpDataFrameHeader::kFrameHeaderSize));
}

void DataFrameRoundTrips(
    std::array<uint8_t, TcpDataFrameHeader::kFrameHeaderSize> input) {
  auto parsed = TcpDataFrameHeader::Parse(input.data());
  if (!parsed.ok()) return;
  uint8_t buffer[TcpDataFrameHeader::kFrameHeaderSize];
  parsed->Serialize(buffer);
  EXPECT_EQ(std::string(input.begin(), input.end()),
            std::string(buffer, buffer + TcpDataFrameHeader::kFrameHeaderSize));
}
FUZZ_TEST(DataFrameHeaderTest, DataFrameRoundTrips);

}  // namespace chaotic_good::data_endpoints_detail

class DataEndpointsTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;
};

chaotic_good::data_endpoints_detail::Clock* Time1Clock() {
  class Clock final : public chaotic_good::data_endpoints_detail::Clock {
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

grpc_event_engine::experimental::Slice DataFrameHeader(
    uint64_t header_length, uint64_t payload_tag, uint64_t send_time,
    uint32_t payload_length) {
  DCHECK_GE(header_length, chaotic_good::TcpDataFrameHeader::kFrameHeaderSize);
  std::vector<uint8_t> buffer(header_length);
  chaotic_good::TcpDataFrameHeader{payload_tag, send_time, payload_length}
      .Serialize(buffer.data());
  return grpc_event_engine::experimental::Slice::FromCopiedBuffer(
      buffer.data(), buffer.size());
}

grpc_event_engine::experimental::Slice PaddingBytes(uint32_t padding) {
  std::vector<uint8_t> buffer(padding);
  return grpc_event_engine::experimental::Slice::FromCopiedBuffer(
      buffer.data(), buffer.size());
}

DATA_ENDPOINTS_TEST(CanWrite) {
  util::testing::MockPromiseEndpoint ep(1234);
  EXPECT_CALL(*ep.endpoint, GetMetricKey("delivery_rate"))
      .WillOnce(::testing::Return(1));
  auto close_ep =
      ep.ExpectDelayedReadClose(absl::OkStatus(), event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(event_engine(), nullptr),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      Time1Clock());
  ep.ExpectWrite(
      {DataFrameHeader(64, 123, 1, 5),
       grpc_event_engine::experimental::Slice::FromCopiedString("hello"),
       PaddingBytes(64 - 5)},
      event_engine().get());
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(123, SliceBuffer(Slice::FromCopiedString("hello")),
                           nullptr));
  WaitForAllPendingWork();
  close_ep();
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanMultiWrite) {
  util::testing::MockPromiseEndpoint ep1(1234);
  util::testing::MockPromiseEndpoint ep2(1235);
  EXPECT_CALL(*ep1.endpoint, GetMetricKey("delivery_rate"))
      .WillOnce(::testing::Return(1));
  EXPECT_CALL(*ep2.endpoint, GetMetricKey("delivery_rate"))
      .WillOnce(::testing::Return(2));
  auto close_ep1 =
      ep1.ExpectDelayedReadClose(absl::OkStatus(), event_engine().get());
  auto close_ep2 =
      ep2.ExpectDelayedReadClose(absl::OkStatus(), event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep1.promise_endpoint),
                std::move(ep2.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(event_engine(), nullptr),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      Time1Clock());
  SliceBuffer writes;
  ep1.CaptureWrites(writes, event_engine().get());
  ep2.CaptureWrites(writes, event_engine().get());
  SpawnTestSeqWithoutContext(
      "write",
      data_endpoints.Write(123, SliceBuffer(Slice::FromCopiedString("hello")),
                           nullptr),
      data_endpoints.Write(124, SliceBuffer(Slice::FromCopiedString("world")),
                           nullptr));
  TickUntilTrue([&]() { return writes.Length() == 2 * (64 + 64); });
  WaitForAllPendingWork();
  close_ep1();
  close_ep2();
  WaitForAllPendingWork();
  auto expected = [](uint64_t payload_tag, std::string payload) {
    auto padding = [](uint32_t padding) {
      std::vector<uint8_t> buffer(padding);
      return Slice::FromCopiedBuffer(buffer.data(), buffer.size());
    };
    SliceBuffer buffer;
    chaotic_good::TcpDataFrameHeader{payload_tag, 1,
                                     static_cast<uint32_t>(payload.length())}
        .Serialize(
            buffer.AddTiny(chaotic_good::TcpDataFrameHeader::kFrameHeaderSize));
    buffer.Append(
        padding(64 - chaotic_good::TcpDataFrameHeader::kFrameHeaderSize));
    buffer.Append(Slice::FromCopiedBuffer(payload));
    buffer.Append(padding(64 - payload.length()));
    return buffer.JoinIntoString();
  };
  EXPECT_THAT(
      writes.JoinIntoString(),
      ::testing::AnyOf(expected(123, "hello") + expected(124, "world"),
                       expected(124, "world") + expected(123, "hello")));
}

DATA_ENDPOINTS_TEST(CanRead) {
  util::testing::MockPromiseEndpoint ep(1234);
  EXPECT_CALL(*ep.endpoint, GetMetricKey("delivery_rate"))
      .WillOnce(::testing::Return(1));
  ep.ExpectRead({DataFrameHeader(64, 5, 1, 5)}, event_engine().get());
  ep.ExpectRead(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello"),
       PaddingBytes(64 - 5)},
      event_engine().get());
  auto close_ep =
      ep.ExpectDelayedReadClose(absl::OkStatus(), event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(event_engine(), nullptr),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      Time1Clock());
  SpawnTestSeqWithoutContext("read", data_endpoints.Read(5).Await(),
                             [](absl::StatusOr<SliceBuffer> result) {
                               EXPECT_TRUE(result.ok());
                               EXPECT_EQ(result->JoinIntoString(), "hello");
                             });
  WaitForAllPendingWork();
  close_ep();
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
