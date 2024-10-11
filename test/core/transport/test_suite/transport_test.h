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

#ifndef GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TRANSPORT_TEST_H
#define GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TRANSPORT_TEST_H

#include <memory>
#include <queue>

#include "absl/random/bit_gen_ref.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

namespace grpc_core {

struct ClientAndServerTransportPair {
  OrphanablePtr<Transport> client;
  OrphanablePtr<Transport> server;
};

using TransportFixture = absl::AnyInvocable<ClientAndServerTransportPair(
    std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>)
                                                const>;

class TransportTest : public YodelTest {
 protected:
  TransportTest(const TransportFixture& fixture,
                const fuzzing_event_engine::Actions& actions,
                absl::BitGenRef rng)
      : YodelTest(actions, rng), fixture_(std::move(fixture)) {}

  void SetServerCallDestination();
  CallInitiator CreateCall(ClientMetadataHandle client_initial_metadata);

  CallHandler TickUntilServerCall();

 private:
  class ServerCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override;
    void Orphaned() override {}
    absl::optional<CallHandler> PopHandler();

   private:
    std::queue<CallHandler> handlers_;
  };

  void InitTest() override { transport_pair_ = fixture_(event_engine()); }

  void Shutdown() override {
    transport_pair_.client.reset();
    transport_pair_.server.reset();
  }

  RefCountedPtr<ServerCallDestination> server_call_destination_ =
      MakeRefCounted<ServerCallDestination>();
  const TransportFixture& fixture_;
  ClientAndServerTransportPair transport_pair_;
};

}  // namespace grpc_core

#define TRANSPORT_TEST(name) YODEL_TEST_P(TransportTest, TransportFixture, name)

#define TRANSPORT_FIXTURE(name)                                                \
  static grpc_core::ClientAndServerTransportPair name(                         \
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>     \
          event_engine);                                                       \
  YODEL_TEST_PARAM(TransportTest, TransportFixture, name, name);               \
  static grpc_core::ClientAndServerTransportPair name(                         \
      GRPC_UNUSED                                                              \
          std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine> \
              event_engine)

#endif  // GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TRANSPORT_TEST_H
