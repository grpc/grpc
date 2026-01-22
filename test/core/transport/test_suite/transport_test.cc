// Copyright 2023 gRPC authors.
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

#include "test/core/transport/test_suite/transport_test.h"

#include <initializer_list>

#include "absl/random/random.h"

namespace grpc_core {

ClientAndServerTransportPair (*g_create_transport_test_fixture)(
    std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>) =
    nullptr;

///////////////////////////////////////////////////////////////////////////////
// TransportTest

void TransportTest::SetServerCallDestination() {
  transport_pair_.server->server_transport()->SetCallDestination(
      server_call_destination_);
}

CallInitiator TransportTest::CreateCall(
    ClientMetadataHandle client_initial_metadata) {
  auto call = MakeCall(std::move(client_initial_metadata));
  call.handler.SpawnInfallible(
      "start-call", [this, handler = call.handler]() mutable {
        transport_pair_.client->client_transport()->StartCall(
            handler.StartCall());
      });
  return std::move(call.initiator);
}

CallHandler TransportTest::TickUntilServerCall() {
  auto poll = [this]() -> Poll<CallHandler> {
    auto handler = server_call_destination_->PopHandler();
    if (handler.has_value()) return std::move(*handler);
    return Pending();
  };
  return TickUntil(absl::FunctionRef<Poll<CallHandler>()>(poll));
}

///////////////////////////////////////////////////////////////////////////////
// TransportTest::ServerCallDestination

void TransportTest::ServerCallDestination::StartCall(
    UnstartedCallHandler handler) {
  handlers_.push(handler.StartCall());
}

std::optional<CallHandler> TransportTest::ServerCallDestination::PopHandler() {
  if (handlers_.empty()) return std::nullopt;
  auto handler = std::move(handlers_.front());
  handlers_.pop();
  return handler;
}

}  // namespace grpc_core
