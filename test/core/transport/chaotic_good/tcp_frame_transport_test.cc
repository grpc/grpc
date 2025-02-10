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

#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

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
    const std::shared_ptr<FuzzingEventEngine>& engine) {
  std::shared_ptr<InterActivityLatch<PromiseEndpoint>> client_latch =
      std::make_shared<InterActivityLatch<PromiseEndpoint>>();
  std::shared_ptr<InterActivityLatch<PromiseEndpoint>> server_latch =
      std::make_shared<InterActivityLatch<PromiseEndpoint>>();
  auto [client, server] = CreatePromiseEndpointPair(engine);
  engine->Run([client_latch, client = std::move(client)]() mutable {
    client_latch->Set(std::move(client));
  });
  engine->Run([server_latch, server = std::move(server)]() mutable {
    server_latch->Set(std::move(server));
  });
  return std::pair(
      PendingConnection("foo",
                        [client_latch]() { return client_latch->Wait(); }),
      PendingConnection("foo",
                        [server_latch]() { return server_latch->Wait(); }));
}

void CanSendFrames(size_t num_data_endpoints, uint32_t client_alignment,
                   uint32_t server_alignment,
                   uint32_t client_inlined_payload_size_threshold,
                   uint32_t server_inlined_payload_size_threshold,
                   const fuzzing_event_engine::Actions& actions) {
  auto engine = std::make_shared<FuzzingEventEngine>(
      FuzzingEventEngine::Options(), actions);
  std::vector<PendingConnection> pending_connections_client;
  std::vector<PendingConnection> pending_connections_server;
  for (size_t i = 0; i < num_data_endpoints; i++) {
    auto [client, server] = CreatePendingConnectionPair(engine);
    pending_connections_client.emplace_back(std::move(client));
    pending_connections_server.emplace_back(std::move(server));
  }
  auto [client, server] = CreatePromiseEndpointPair(engine);
  auto client_transport = MakeRefCounted<TcpFrameTransport>(
      TcpFrameTransport::Options{server_alignment, client_alignment,
                                 server_inlined_payload_size_threshold},
      std::move(client), std::move(pending_connections_client));
  auto server_transport = MakeRefCounted<TcpFrameTransport>(
      TcpFrameTransport::Options{client_alignment, server_alignment,
                                 client_inlined_payload_size_threshold},
      std::move(server), std::move(pending_connections_server));
  auto client_arena = SimpleArenaAllocator()->MakeArena();
  auto server_arena = SimpleArenaAllocator()->MakeArena();
  client_arena->SetContext(static_cast<EventEngine*>(engine.get()));
  server_arena->SetContext(static_cast<EventEngine*>(engine.get()));
  auto client_party = Party::Make(client_arena);
  auto server_party = Party::Make(server_arena);
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core
