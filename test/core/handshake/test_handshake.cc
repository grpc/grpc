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

#include "test/core/handshake/test_handshake.h"

#include <memory>

#include "absl/cleanup/cleanup.h"
#include "src/core/config/core_configuration.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::grpc_event_engine_endpoint_create;
using ::grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using ::grpc_event_engine::experimental::ResolvedAddressMakeWild4;
using ::grpc_event_engine::experimental::SetDefaultEventEngine;
using ::grpc_event_engine::experimental::ShutdownDefaultEventEngine;

namespace grpc_core {

namespace {
void Handshake(HandshakerType handshaker_type,
               OrphanablePtr<grpc_endpoint> endpoint,
               const ChannelArgs& channel_args,
               absl::optional<absl::StatusOr<ChannelArgs>>* output) {
  auto handshake_mgr = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      handshaker_type, channel_args, nullptr, handshake_mgr.get());
  handshake_mgr->DoHandshake(
      std::move(endpoint), channel_args, Timestamp::Now() + Duration::Hours(24),
      nullptr, [handshake_mgr, output](absl::StatusOr<HandshakerArgs*> result) {
        if (!result.ok()) {
          output->emplace(result.status());
        } else {
          output->emplace((*result)->args);
        }
      });
}
}  // namespace

absl::StatusOr<std::tuple<ChannelArgs, ChannelArgs>> TestHandshake(
    ChannelArgs client_args, ChannelArgs server_args,
    const fuzzing_event_engine::Actions& actions) {
  CHECK(IsEventEngineClientEnabled());
  CHECK(IsEventEngineListenerEnabled());
  grpc_timer_manager_set_start_threaded(false);
  grpc_init();
  const int kPort = 1234;
  // Configure default event engine
  auto engine = std::make_shared<FuzzingEventEngine>(
      FuzzingEventEngine::Options(), actions);
  auto cleanup = absl::MakeCleanup([&engine, &client_args, &server_args]() {
    // Remove all references held to the event engine
    engine.reset();
    client_args = ChannelArgs();
    server_args = ChannelArgs();
    // And quiesce
    ShutdownDefaultEventEngine();
  });
  SetDefaultEventEngine(std::static_pointer_cast<EventEngine>(engine));
  // Address - just ok for fuzzing ee
  const auto addr = ResolvedAddressMakeWild4(kPort);
  // Pass event engine down
  client_args =
      client_args
          .SetObject<EventEngine>(std::static_pointer_cast<EventEngine>(engine))
          .Set(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS,
               ResolvedAddressToURI(addr).value());
  server_args = server_args.SetObject<EventEngine>(
      std::static_pointer_cast<EventEngine>(engine));
  // Start listening
  absl::optional<absl::StatusOr<ChannelArgs>> output_server_args;
  auto listener = engine->CreateListener(
      [output_server_args = &output_server_args, server_args](
          std::unique_ptr<EventEngine::Endpoint> endpoint, MemoryAllocator) {
        Handshake(HandshakerType::HANDSHAKER_SERVER,
                  OrphanablePtr<grpc_endpoint>(
                      grpc_event_engine_endpoint_create(std::move(endpoint))),
                  server_args, output_server_args);
      },
      [](const absl::Status&) {}, ChannelArgsEndpointConfig(server_args),
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          ResourceQuota::Default()->memory_quota()));
  if (!listener.ok()) return listener.status();
  auto bind_status = (*listener)->Bind(addr);
  if (!bind_status.ok()) return bind_status.status();
  auto listen_status = (*listener)->Start();
  if (!listen_status.ok()) return listen_status;
  // Connect client
  absl::optional<absl::StatusOr<ChannelArgs>> output_client_args;
  Handshake(HANDSHAKER_CLIENT, nullptr, client_args, &output_client_args);
  // Await completion
  std::optional<absl::StatusOr<std::tuple<ChannelArgs, ChannelArgs>>> result;
  while (!result.has_value()) {
    if (output_client_args.has_value() && !output_client_args->ok()) {
      result.emplace(output_client_args->status());
    } else if (output_server_args.has_value() && !output_server_args->ok()) {
      result.emplace(output_server_args->status());
    } else if (output_client_args.has_value() &&
               output_server_args.has_value()) {
      result.emplace(std::make_tuple(
          (*output_client_args)
              ->SetObject<EventEngine>(std::shared_ptr<EventEngine>()),
          (*output_server_args)
              ->SetObject<EventEngine>(std::shared_ptr<EventEngine>())));
    } else {
      engine->Tick();
    }
  }
  engine->TickUntilIdle();
  return *result;
}

}  // namespace grpc_core
