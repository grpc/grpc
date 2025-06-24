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
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

namespace grpc_core {

namespace chaotic_good::data_endpoints_detail {

struct StartSendOp {
  uint64_t bytes;
};

struct SetNetworkMetricsOp {
  SendRate::NetworkSend network_send;
  SendRate::NetworkMetrics metrics;
};

struct CheckDeliveryTime {
  uint64_t current_time;
  uint64_t bytes;
};

using SendRateOp =
    std::variant<StartSendOp, SetNetworkMetricsOp, CheckDeliveryTime>;

void SendRateIsRobust(double initial_rate, std::vector<SendRateOp> ops) {
  SendRate send_rate(initial_rate);
  for (const auto& op : ops) {
    Match(
        op, [&](StartSendOp op) { send_rate.StartSend(op.bytes); },
        [&](SetNetworkMetricsOp op) {
          send_rate.SetNetworkMetrics(op.network_send, op.metrics);
        },
        [&](CheckDeliveryTime op) {
          auto delivery_time_calculator =
              send_rate.GetDeliveryData(op.current_time);
          const auto delivery_time =
              delivery_time_calculator.start_time +
              op.bytes / delivery_time_calculator.bytes_per_second;
          EXPECT_FALSE(std::isnan(delivery_time));
          EXPECT_GE(delivery_time, 0.0);
        });
  }
}
FUZZ_TEST(SendRateTest, SendRateIsRobust)
    .WithDomains(fuzztest::InRange<double>(1e-9, 1e9),
                 fuzztest::Arbitrary<std::vector<SendRateOp>>());

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

MpscQueued<chaotic_good::OutgoingFrame> TestFrame(absl::string_view payload) {
  // We create an mpsc receiver that we can funnel frames through to get them
  // properly wrapped in an MpscQueued so that we don't need to special case
  // resource reclamation for DataEndpoints.
  static MpscReceiver<chaotic_good::OutgoingFrame>* frames =
      new MpscReceiver<chaotic_good::OutgoingFrame>(1000000);
  static Mutex* mu = new Mutex();
  MutexLock lock(mu);
  chaotic_good::MessageFrame frame(
      1, Arena::MakePooled<Message>(
             SliceBuffer(Slice::FromCopiedString(payload)), 0));
  frames->MakeSender().UnbufferedImmediateSend(
      chaotic_good::OutgoingFrame{std::move(frame), nullptr}, 1);
  return std::move(*frames->Next()().value());
}

void ExportMockTelemetryInfo(util::testing::MockPromiseEndpoint& ep) {
  auto telemetry_info = std::make_shared<util::testing::MockTelemetryInfo>();
  EXPECT_CALL(*ep.endpoint, GetTelemetryInfo())
      .WillOnce(::testing::Return(telemetry_info));
  EXPECT_CALL(*telemetry_info, GetMetricKey("delivery_rate"))
      .WillOnce(::testing::Return(1));
  EXPECT_CALL(*telemetry_info, GetMetricKey("net_rtt_usec"))
      .WillOnce(::testing::Return(2));
  EXPECT_CALL(*telemetry_info, GetMetricKey("data_notsent"))
      .WillOnce(::testing::Return(3));
  EXPECT_CALL(*telemetry_info, GetMetricKey("byte_offset"))
      .WillOnce(::testing::Return(4));
}

RefCountedPtr<channelz::SocketNode> MakeTestChannelzSocketNode() {
  return MakeRefCounted<channelz::SocketNode>("from", "to", "test", nullptr);
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
GetPeerAddress() {
  static grpc_event_engine::experimental::EventEngine::ResolvedAddress
      peer_address = grpc_event_engine::experimental::URIToResolvedAddress(
                         "ipv4:127.0.0.1:1234")
                         .value();
  return peer_address;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
GetLocalAddress() {
  static grpc_event_engine::experimental::EventEngine::ResolvedAddress
      peer_address = grpc_event_engine::experimental::URIToResolvedAddress(
                         "ipv4:127.0.0.1:4321")
                         .value();
  return peer_address;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
GetPeerAddress2() {
  static grpc_event_engine::experimental::EventEngine::ResolvedAddress
      peer_address = grpc_event_engine::experimental::URIToResolvedAddress(
                         "ipv4:127.0.0.1:2345")
                         .value();
  return peer_address;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
GetLocalAddress2() {
  static grpc_event_engine::experimental::EventEngine::ResolvedAddress
      peer_address = grpc_event_engine::experimental::URIToResolvedAddress(
                         "ipv4:127.0.0.1:5432")
                         .value();
  return peer_address;
}

DATA_ENDPOINTS_TEST(CanWrite) {
  util::testing::MockPromiseEndpoint ep(1234);
  EXPECT_CALL(*ep.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress()));
  EXPECT_CALL(*ep.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress()));
  ExportMockTelemetryInfo(ep);
  auto close_ep = ep.ExpectDelayedReadClose(absl::UnavailableError("test done"),
                                            event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(
          event_engine(), MakeTestChannelzSocketNode()),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      "rand", Time1Clock());
  ep.ExpectWrite(
      {DataFrameHeader(64, 123, 1, 5),
       grpc_event_engine::experimental::Slice::FromCopiedString("hello"),
       PaddingBytes(64 - 5)},
      event_engine().get());
  data_endpoints.Write(123, TestFrame("hello"));
  WaitForAllPendingWork();
  close_ep();
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanMultiWrite) {
  util::testing::MockPromiseEndpoint ep1(1234);
  util::testing::MockPromiseEndpoint ep2(1235);
  EXPECT_CALL(*ep1.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress()));
  EXPECT_CALL(*ep1.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress()));
  EXPECT_CALL(*ep2.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress2()));
  EXPECT_CALL(*ep2.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress2()));
  ExportMockTelemetryInfo(ep1);
  ExportMockTelemetryInfo(ep2);
  auto close_ep1 = ep1.ExpectDelayedReadClose(
      absl::UnavailableError("test done"), event_engine().get());
  auto close_ep2 = ep2.ExpectDelayedReadClose(
      absl::UnavailableError("test done"), event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep1.promise_endpoint),
                std::move(ep2.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(
          event_engine(), MakeTestChannelzSocketNode()),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      "spanrr", Time1Clock());
  SliceBuffer writes;
  ep1.CaptureWrites(writes, event_engine().get());
  ep2.CaptureWrites(writes, event_engine().get());
  data_endpoints.Write(123, TestFrame("hello"));
  data_endpoints.Write(124, TestFrame("world"));
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
  EXPECT_CALL(*ep.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress()));
  EXPECT_CALL(*ep.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress()));
  ExportMockTelemetryInfo(ep);
  ep.ExpectRead({DataFrameHeader(64, 5, 1, 5)}, event_engine().get());
  ep.ExpectRead(
      {grpc_event_engine::experimental::Slice::FromCopiedString("hello"),
       PaddingBytes(64 - 5)},
      event_engine().get());
  auto close_ep = ep.ExpectDelayedReadClose(absl::UnavailableError("test done"),
                                            event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(
          event_engine(), MakeTestChannelzSocketNode()),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      "spanrr", Time1Clock());
  SpawnTestSeqWithoutContext("read", data_endpoints.Read(5).Await(),
                             [](absl::StatusOr<SliceBuffer> result) {
                               EXPECT_TRUE(result.ok());
                               EXPECT_EQ(result->JoinIntoString(), "hello");
                             });
  WaitForAllPendingWork();
  close_ep();
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanWriteSecurityFrame) {
  util::testing::MockPromiseEndpoint ep(1234);
  EXPECT_CALL(*ep.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress()));
  EXPECT_CALL(*ep.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress()));
  auto* transport_framing_endpoint_extension = ep.endpoint->AddExtension<
      util::testing::MockTransportFramingEndpointExtension>();
  absl::AnyInvocable<void(SliceBuffer*)> send_frame_callback;
  EXPECT_CALL(*transport_framing_endpoint_extension, SetSendFrameCallback)
      .WillOnce(::testing::SaveArgByMove<0>(&send_frame_callback));
  ExportMockTelemetryInfo(ep);
  auto close_ep = ep.ExpectDelayedReadClose(absl::UnavailableError("test done"),
                                            event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(
          event_engine(), MakeTestChannelzSocketNode()),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      "rand", Time1Clock());
  ::testing::Mock::VerifyAndClearExpectations(
      transport_framing_endpoint_extension);
  ep.ExpectWrite({DataFrameHeader(64, 0, 0, strlen("security_frame_bytes")),
                  grpc_event_engine::experimental::Slice::FromCopiedString(
                      "security_frame_bytes"),
                  PaddingBytes(64 - strlen("security_frame_bytes"))},
                 event_engine().get());
  SliceBuffer security_frame_bytes(
      Slice::FromCopiedString("security_frame_bytes"));
  send_frame_callback(&security_frame_bytes);
  WaitForAllPendingWork();
  close_ep();
  WaitForAllPendingWork();
}

DATA_ENDPOINTS_TEST(CanReadSecurityFrame) {
  util::testing::MockPromiseEndpoint ep(1234);
  EXPECT_CALL(*ep.endpoint, GetPeerAddress())
      .WillRepeatedly(::testing::ReturnRef(GetPeerAddress()));
  EXPECT_CALL(*ep.endpoint, GetLocalAddress())
      .WillRepeatedly(::testing::ReturnRef(GetLocalAddress()));
  auto* transport_framing_endpoint_extension =
      ep.endpoint->AddExtension<::testing::StrictMock<
          util::testing::MockTransportFramingEndpointExtension>>();
  ExportMockTelemetryInfo(ep);
  EXPECT_CALL(*transport_framing_endpoint_extension, SetSendFrameCallback)
      .WillOnce(::testing::Return());
  EXPECT_CALL(*transport_framing_endpoint_extension, ReceiveFrame)
      .WillOnce([](SliceBuffer buffer) {
        EXPECT_EQ(buffer.JoinIntoString(), "security_frame_bytes");
      });
  ep.ExpectRead({DataFrameHeader(64, 0, 0, strlen("security_frame_bytes"))},
                event_engine().get());
  ep.ExpectRead({grpc_event_engine::experimental::Slice::FromCopiedString(
                     "security_frame_bytes"),
                 PaddingBytes(64 - strlen("security_frame_bytes"))},
                event_engine().get());
  auto close_ep = ep.ExpectDelayedReadClose(absl::UnavailableError("test done"),
                                            event_engine().get());
  chaotic_good::DataEndpoints data_endpoints(
      Endpoints(std::move(ep.promise_endpoint)),
      MakeRefCounted<chaotic_good::TransportContext>(
          event_engine(), MakeTestChannelzSocketNode()),
      64, 64, std::make_shared<chaotic_good::TcpZTraceCollector>(), false,
      "rand", Time1Clock());
  SpawnTestSeqWithoutContext(
      "read",
      [&data_endpoints]() {
        return Race(data_endpoints.Read(12345).Await(),
                    Map(Sleep(Duration::Minutes(1)),
                        [](absl::Status status) -> absl::StatusOr<SliceBuffer> {
                          EXPECT_TRUE(status.ok()) << status;
                          return absl::CancelledError("test");
                        }));
      },
      [](absl::StatusOr<SliceBuffer> result) { EXPECT_FALSE(result.ok()); });
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

TEST(DataEndpointsTest, CanWriteRegression) {
  CanWrite(ParseTestProto(
      R"pb(event_engine_actions {
             run_delay: 0
             run_delay: 9223372036854775807
             assign_ports: 2147483647
             endpoint_metrics {}
           }
      )pb"));
}

TEST(DataEndpointsTest, CanWriteRegression2) {
  CanWrite(ParseTestProto(R"pb(event_engine_actions {
                                 assign_ports: 4142908857
                                 endpoint_metrics {}
                                 returned_endpoint_metrics {
                                   write_id: 3446018212
                                   event: 3334425759
                                 }
                               }
                               rng: 14323299152728827054
  )pb"));
}

}  // namespace grpc_core
