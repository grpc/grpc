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
#include <memory>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/postmortem_emit.h"
#include "test/core/transport/chaotic_good/test_frame.h"
#include "test/core/transport/chaotic_good/test_frame.pb.h"

using fuzztest::Arbitrary;
using fuzztest::InRange;
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
      : DataSource(std::move(node)), party_(std::move(party)) {}

  ~PartyExposer() { ResetDataSource(); }

  void AddData(channelz::DataSink sink) override {
    party_->ToJson([sink, name = name_](Json::Object obj) mutable {
      sink.AddAdditionalInfo(name, std::move(obj));
    });
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
  }

 private:
  std::map<size_t, const Frame*> expected_frames_;
  std::vector<std::unique_ptr<PartyExposer>> parties_;
  size_t next_frame_ = 0;
  EventEngine* const event_engine_;
  RefCountedPtr<channelz::BaseNode> channelz_node_;
};

RefCountedPtr<channelz::SocketNode> MakeTestChannelzSocketNode(
    std::string name) {
  return MakeRefCounted<channelz::SocketNode>("from", "to", name, nullptr);
}

void CanSendFrames(size_t num_data_endpoints, uint32_t client_alignment,
                   uint32_t server_alignment,
                   uint32_t client_inlined_payload_size_threshold,
                   uint32_t server_inlined_payload_size_threshold,
                   size_t client_max_buffer_hint, size_t server_max_buffer_hint,
                   const fuzzing_event_engine::Actions& actions,
                   std::vector<Frame> send_on_client_frames,
                   std::vector<Frame> send_on_server_frames) {
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
  auto client_transport = MakeOrphanable<TcpFrameTransport>(
      TcpFrameTransport::Options{server_alignment, client_alignment,
                                 server_inlined_payload_size_threshold},
      std::move(client), std::move(pending_connections_client),
      MakeRefCounted<TransportContext>(
          std::static_pointer_cast<EventEngine>(engine), client_node));
  auto server_node = MakeTestChannelzSocketNode("server");
  auto server_transport = MakeOrphanable<TcpFrameTransport>(
      TcpFrameTransport::Options{client_alignment, server_alignment,
                                 client_inlined_payload_size_threshold},
      std::move(server), std::move(pending_connections_server),
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
  while (!client_sink->done() || !server_sink->done()) {
    engine->Tick();
    if (Timestamp::Now() > deadline) {
      PostMortemEmit();
      LOG(FATAL) << "6 hour deadline exceeded";
    }
  }
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
        /* send_on_server_frames */ VectorOf(AnyFrame()));

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

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core
