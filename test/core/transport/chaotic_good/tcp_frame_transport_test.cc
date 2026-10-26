// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/tcp_frame_transport.h"

#include <google/protobuf/text_format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "fuzztest/fuzztest.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/util/postmortem_emit.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/transport/chaotic_good/test_frame.h"
#include "test/core/transport/chaotic_good/test_frame.pb.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

using fuzztest::Arbitrary;
using fuzztest::InRange;
using fuzztest::Just;
using fuzztest::VectorOf;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;

namespace grpc_core {
namespace chaotic_good {
namespace {

std::pair<PromiseEndpoint, PromiseEndpoint> CreatePromiseEndpointPair(
    const std::shared_ptr<FuzzingEventEngine>& engine) {
  auto [client, server] = engine->CreateEndpointPair();
  return std::pair(PromiseEndpoint(std::move(client), SliceBuffer{}),
                   PromiseEndpoint(std::move(server), SliceBuffer{}));
}

std::pair<PendingConnection, PendingConnection> CreatePendingConnectionPair(
    const std::shared_ptr<FuzzingEventEngine>& engine, size_t i) {
  std::shared_ptr<InterActivityLatch<PromiseEndpoint>> client_latch =
      std::make_shared<InterActivityLatch<PromiseEndpoint>>();
  std::shared_ptr<InterActivityLatch<PromiseEndpoint>> server_latch =
      std::make_shared<InterActivityLatch<PromiseEndpoint>>();
  auto [client, server] = CreatePromiseEndpointPair(engine);
  engine->Run([client_latch, client = std::move(client), i]() mutable {
    GRPC_TRACE_LOG(chaotic_good, INFO) << "mark client " << i << " available";
    client_latch->Set(std::move(client));
  });
  engine->Run([server_latch, server = std::move(server), i]() mutable {
    GRPC_TRACE_LOG(chaotic_good, INFO) << "mark server " << i << " available";
    server_latch->Set(std::move(server));
  });
  return std::pair(
      PendingConnection(
          "foo", Map(client_latch->Wait(),
                     [client_latch,
                      i](PromiseEndpoint e) -> absl::StatusOr<PromiseEndpoint> {
                       GRPC_TRACE_LOG(chaotic_good, INFO)
                           << "client " << i << " no longer pending";
                       return std::move(e);
                     })),
      PendingConnection(
          "foo", Map(server_latch->Wait(),
                     [server_latch,
                      i](PromiseEndpoint e) -> absl::StatusOr<PromiseEndpoint> {
                       GRPC_TRACE_LOG(chaotic_good, INFO)
                           << "server " << i << " no longer pending";
                       return std::move(e);
                     })));
}

class PartyExposer final : public channelz::DataSource {
 public:
  PartyExposer(absl::string_view name, RefCountedPtr<channelz::BaseNode> node,
               RefCountedPtr<Party> party)
      : DataSource(std::move(node)), party_(std::move(party)) {
    SourceConstructed();
  }

  ~PartyExposer() { SourceDestructing(); }

  void AddData(channelz::DataSink sink) override {
    party_->ExportToChannelz(std::string(name_), sink);
  }

 private:
  absl::string_view name_;
  RefCountedPtr<Party> party_;
};

class TestSink : public FrameTransportSink {
 public:
  TestSink(absl::Span<const Frame> expected_frames, EventEngine* event_engine,
           RefCountedPtr<channelz::BaseNode> channelz_node)
      : event_engine_(event_engine), channelz_node_(std::move(channelz_node)) {
    for (size_t i = 0; i < expected_frames.size(); i++) {
      expected_frames_[i] = &expected_frames[i];
    }
  }

  bool done() const { return expected_frames_.empty(); }

  bool closed() const { return closed_; }

  absl::Status closed_status() const { return closed_status_; }

  void OnIncomingFrame(IncomingFrame incoming_frame) override {
    const size_t frame_id = next_frame_;
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << this << " OnIncomingFrame: " << frame_id;
    ++next_frame_;
    CHECK_EQ(expected_frames_.count(frame_id), 1);
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext(event_engine_);
    auto party = Party::Make(arena);
    party->Spawn(
        "read-frame",
        [incoming_frame = std::move(incoming_frame), frame_id,
         self = RefAsSubclass<TestSink>()]() mutable {
          return Seq(incoming_frame.Payload(),
                     [frame_id, self](absl::StatusOr<Frame> frame) {
                       GRPC_TRACE_LOG(chaotic_good, INFO)
                           << self.get() << " Got payload for " << frame_id;
                       CHECK(frame.ok()) << frame.status();
                       auto it = self->expected_frames_.find(frame_id);
                       CHECK(it != self->expected_frames_.end())
                           << " frame " << frame_id << " not found";
                       const Frame* expected_frame = it->second;
                       CHECK_EQ(*frame, *expected_frame);
                       self->expected_frames_.erase(it);
                       return absl::OkStatus();
                     });
        },
        [](const absl::Status&) {});
    parties_.push_back(std::make_unique<PartyExposer>("incoming_frame",
                                                      channelz_node_, party));
  }

  void OnFrameTransportClosed(absl::Status status) override {
    LOG(INFO) << "transport closed: " << status;
    closed_ = true;
    closed_status_ = status;
  }

 private:
  std::map<size_t, const Frame*> expected_frames_;
  std::vector<std::unique_ptr<PartyExposer>> parties_;
  size_t next_frame_ = 0;
  EventEngine* const event_engine_;
  RefCountedPtr<channelz::BaseNode> channelz_node_;
  bool closed_ = false;
  absl::Status closed_status_;
};

RefCountedPtr<channelz::SocketNode> MakeTestChannelzSocketNode(
    std::string name) {
  return MakeRefCounted<channelz::SocketNode>("from", "to", name, nullptr);
}

void CanSendFrames(
    size_t num_data_endpoints, uint32_t client_alignment,
    uint32_t server_alignment, uint32_t client_inlined_payload_size_threshold,
    uint32_t server_inlined_payload_size_threshold,
    size_t client_max_buffer_hint, size_t server_max_buffer_hint,
    const fuzzing_event_engine::Actions& actions,
    std::vector<Frame> send_on_client_frames,
    std::vector<Frame> send_on_server_frames,
    uint32_t client_max_receive_message_length =
        std::numeric_limits<uint32_t>::max(),
    uint32_t server_max_receive_message_length =
        std::numeric_limits<uint32_t>::max(),
    absl::StatusCode expected_client_status = absl::StatusCode::kOk,
    absl::StatusCode expected_server_status = absl::StatusCode::kOk) {
  grpc_tracer_init();
  auto engine = std::make_shared<FuzzingEventEngine>(
      FuzzingEventEngine::Options(), actions);
  std::vector<PendingConnection> pending_connections_client;
  std::vector<PendingConnection> pending_connections_server;
  for (size_t i = 0; i < num_data_endpoints; i++) {
    auto [client, server] = CreatePendingConnectionPair(engine, i);
    pending_connections_client.emplace_back(std::move(client));
    pending_connections_server.emplace_back(std::move(server));
  }
  auto [client, server] = CreatePromiseEndpointPair(engine);
  auto client_node = MakeTestChannelzSocketNode("client");
  TcpFrameTransport::Options client_options;
  client_options.encode_alignment = server_alignment;
  client_options.decode_alignment = client_alignment;
  client_options.inlined_payload_size_threshold =
      server_inlined_payload_size_threshold;
  client_options.max_receive_message_length = client_max_receive_message_length;

  auto client_transport = MakeOrphanable<TcpFrameTransport>(
      client_options, std::move(client), std::move(pending_connections_client),
      MakeRefCounted<TransportContext>(
          std::static_pointer_cast<EventEngine>(engine), client_node));

  TcpFrameTransport::Options server_options;
  server_options.encode_alignment = client_alignment;
  server_options.decode_alignment = server_alignment;
  server_options.inlined_payload_size_threshold =
      client_inlined_payload_size_threshold;
  server_options.max_receive_message_length = server_max_receive_message_length;

  auto server_node = MakeTestChannelzSocketNode("server");
  auto server_transport = MakeOrphanable<TcpFrameTransport>(
      server_options, std::move(server), std::move(pending_connections_server),
      MakeRefCounted<TransportContext>(
          std::static_pointer_cast<EventEngine>(engine), server_node));
  auto client_arena = SimpleArenaAllocator()->MakeArena();
  auto server_arena = SimpleArenaAllocator()->MakeArena();
  client_arena->SetContext(static_cast<EventEngine*>(engine.get()));
  server_arena->SetContext(static_cast<EventEngine*>(engine.get()));
  auto client_party = Party::Make(client_arena);
  auto server_party = Party::Make(server_arena);
  PartyExposer client_party_exposer("client_transport_party", client_node,
                                    client_party);
  PartyExposer server_party_exposer("server_transport_party", server_node,
                                    server_party);
  MpscReceiver<OutgoingFrame> client_receiver(client_max_buffer_hint);
  MpscReceiver<OutgoingFrame> server_receiver(server_max_buffer_hint);
  auto client_sender = client_receiver.MakeSender();
  auto server_sender = server_receiver.MakeSender();
  auto client_sink = MakeRefCounted<TestSink>(send_on_server_frames,
                                              engine.get(), client_node);
  auto server_sink = MakeRefCounted<TestSink>(send_on_client_frames,
                                              engine.get(), server_node);
  client_transport->Start(client_party.get(), std::move(client_receiver),
                          client_sink);
  server_transport->Start(server_party.get(), std::move(server_receiver),
                          server_sink);
  for (const auto& frame : send_on_client_frames) {
    client_sender.UnbufferedImmediateSend(
        UntracedOutgoingFrame(CopyFrame(frame)), 1);
  }
  for (const auto& frame : send_on_server_frames) {
    server_sender.UnbufferedImmediateSend(
        UntracedOutgoingFrame(CopyFrame(frame)), 1);
  }
  auto deadline = Timestamp::Now() + Duration::Hours(6);
  while (true) {
    bool client_done_sending_or_closed =
        expected_client_status == absl::StatusCode::kOk ? client_sink->done()
                                                        : client_sink->closed();
    bool server_done_sending_or_closed =
        expected_server_status == absl::StatusCode::kOk ? server_sink->done()
                                                        : server_sink->closed();
    if (client_done_sending_or_closed && server_done_sending_or_closed) {
      break;
    }
    engine->Tick();
    if (Timestamp::Now() > deadline) {
      PostMortemEmit();
      LOG(FATAL) << "6 hour deadline exceeded";
    }
  }

  EXPECT_EQ(client_sink->closed_status().code(), expected_client_status);
  EXPECT_EQ(server_sink->closed_status().code(), expected_server_status);

  client_transport.reset();
  server_transport.reset();
  engine->TickUntilIdle();
}

FUZZ_TEST(TcpFrameTransportTest, CanSendFrames)
    .WithDomains(
        /* num_data_endpoints */ InRange<size_t>(0, 64),
        /* client_alignment */ InRange<uint32_t>(1, 1024),
        /* server_alignment */ InRange<uint32_t>(1, 1024),
        /* client_inlined_payload_size_threshold */
        InRange<uint32_t>(0, 8 * 1024 * 1024),
        /* server_inlined_payload_size_threshold */
        InRange<uint32_t>(0, 8 * 1024 * 1024),
        /* client_max_buffer_hint */ InRange<uint32_t>(1, 1024 * 1024 * 1024),
        /* server_max_buffer_hint */ InRange<uint32_t>(1, 1024 * 1024 * 1024),
        /* actions */ Arbitrary<fuzzing_event_engine::Actions>(),
        /* send_on_client_frames */ VectorOf(AnyFrame()),
        /* send_on_server_frames */ VectorOf(AnyFrame()),
        /* client_max_receive_message_length */
        Just(std::numeric_limits<uint32_t>::max()),
        /* server_max_receive_message_length */
        Just(std::numeric_limits<uint32_t>::max()),
        /* expected_client_status */ Just(absl::StatusCode::kOk),
        /* expected_server_status */ Just(absl::StatusCode::kOk));

auto ParseFuzzingEventEngineProto(const std::string& proto) {
  fuzzing_event_engine::Actions msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

auto ParseFrameProto(const std::string& proto) {
  chaotic_good_frame::TestFrame msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg)) << proto;
  return msg;
}

TEST(TcpFrameTransportTest, CanSendFramesRegression1) {
  std::vector<Frame> send_on_client_frames;
  send_on_client_frames.emplace_back(
      chaotic_good::FrameFromTestFrame(
          ParseFrameProto(R"pb(
            settings {
              max_chunk_size: 2147483647
              supported_features: UNSPECIFIED
            })pb")));
  std::vector<Frame> send_on_server_frames;
  send_on_server_frames.emplace_back(
      chaotic_good::FrameFromTestFrame(ParseFrameProto(
          R"pb(settings { alignment: 1 supported_features: UNSPECIFIED })pb")));
  send_on_server_frames.emplace_back(chaotic_good::FrameFromTestFrame(
      ParseFrameProto(R"pb(begin_message { stream_id: 4294967295 })pb")));
  CanSendFrames(
      33, 1, 567, 0, 3878004, 36, 10, ParseFuzzingEventEngineProto(R"pb(
        run_delay: 9223372036854775807
        endpoint_metrics {}
        returned_endpoint_metrics {
          delay_us: 2147483647
          event: 4010916377
          returned_endpoint_metrics {}
        })pb"),
      std::move(send_on_client_frames), std::move(send_on_server_frames));
}

struct MaxMessageSizeTestParam {
  size_t client_max_receive_message_length;
  size_t server_max_receive_message_length;
  bool fail_on_client;
  size_t num_data_endpoints;
};

class MaxMessageSizeExceeded
    : public ::testing::TestWithParam<MaxMessageSizeTestParam> {};

TEST_P(MaxMessageSizeExceeded, SendsFrames) {
  const auto& param = GetParam();
  std::vector<Frame> send_on_client_frames;
  std::vector<Frame> send_on_server_frames;

  if (param.fail_on_client) {
    send_on_server_frames.emplace_back(
        chaotic_good::FrameFromTestFrame(ParseFrameProto(absl::StrFormat(
            R"pb(
              message { stream_id: 1 all_zeros_length: %d }
            )pb",
            param.client_max_receive_message_length + 1))));
  } else {
    send_on_client_frames.emplace_back(
        chaotic_good::FrameFromTestFrame(ParseFrameProto(absl::StrFormat(
            R"pb(
              message { stream_id: 1 all_zeros_length: %d }
            )pb",
            param.server_max_receive_message_length + 1))));
  }

  CanSendFrames(
      /* num_data_endpoints */ param.num_data_endpoints,
      /* client_alignment */ 1,
      /* server_alignment */ 1,
      /* client_inlined_payload_size_threshold */ 8192,
      /* server_inlined_payload_size_threshold */ 8192,
      /* client_max_buffer_hint */ 1024,
      /* server_max_buffer_hint */ 1024,
      /* actions */ fuzzing_event_engine::Actions(),
      std::move(send_on_client_frames), std::move(send_on_server_frames),
      /* client_max_receive_message_length */
      param.client_max_receive_message_length,
      /* server_max_receive_message_length */
      param.server_max_receive_message_length,
      /* expected_client_status */
      param.fail_on_client ? absl::StatusCode::kResourceExhausted
                           : absl::StatusCode::kOk,
      /* expected_server_status */
      param.fail_on_client ? absl::StatusCode::kOk
                           : absl::StatusCode::kResourceExhausted);
}

INSTANTIATE_TEST_SUITE_P(
    TcpFrameTransportTest, MaxMessageSizeExceeded,
    ::testing::Values(
        // Client max receive message length exceeded on control endpoint with
        // no data endpoints.
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/10000,
                                /*server_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*fail_on_client=*/true,
                                /*num_data_endpoints=*/0},
        // Server max receive message length exceeded on control endpoint with
        // no data endpoints.
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*server_max_receive_message_length=*/10000,
                                /*fail_on_client=*/false,
                                /*num_data_endpoints=*/0},
        // Client max receive message length exceeded on control endpoint with
        // data endpoints (since limit is actually smaller than inlined payload
        // size threshold; this is likely a misconfiguration - but should still
        // fail)
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/100,
                                /*server_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*fail_on_client=*/true,
                                /*num_data_endpoints=*/1},
        // Server max receive message length exceeded on control endpoint with
        // data endpoints (since limit is actually smaller than inlined payload
        // size threshold; this is likely a misconfiguration - but should still
        // fail)
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*server_max_receive_message_length=*/100,
                                /*fail_on_client=*/false,
                                /*num_data_endpoints=*/1},
        // Client max receive message length exceeded on data endpoint. Limit is
        // greater than inlined payload size threshold. This is the most likely
        // scenario for this error.
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/10000,
                                /*server_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*fail_on_client=*/true,
                                /*num_data_endpoints=*/4},
        // Server max receive message length exceeded on data endpoint. Limit is
        // greater than inlined payload size threshold. This is the most likely
        // scenario for this error.
        MaxMessageSizeTestParam{/*client_max_receive_message_length=*/
                                std::numeric_limits<uint32_t>::max(),
                                /*server_max_receive_message_length=*/10000,
                                /*fail_on_client=*/false,
                                /*num_data_endpoints=*/4}));

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core
