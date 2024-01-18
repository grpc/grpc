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

#ifndef GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_FIXTURE_H
#define GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_FIXTURE_H

#include "src/core/lib/transport/transport.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

namespace grpc_core {

class TransportFixture {
 public:
  struct ClientAndServerTransportPair {
    OrphanablePtr<Transport> client;
    OrphanablePtr<Transport> server;
  };
  virtual ~TransportFixture() = default;
  virtual ClientAndServerTransportPair CreateTransportPair(
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine) = 0;
};

class TransportFixtureRegistry {
 public:
  static TransportFixtureRegistry& Get();
  void RegisterFixture(absl::string_view name,
                       absl::AnyInvocable<TransportFixture*() const> create);

  struct Fixture {
    absl::string_view name;
    absl::AnyInvocable<TransportFixture*() const> create;
  };

  const std::vector<Fixture>& fixtures() const { return fixtures_; }

 private:
  std::vector<Fixture> fixtures_;
};

}  // namespace grpc_core

#define TRANSPORT_FIXTURE(name)                                                \
  class TransportFixture_##name : public grpc_core::TransportFixture {         \
   public:                                                                     \
    using TransportFixture::TransportFixture;                                  \
    ClientAndServerTransportPair CreateTransportPair(                          \
        std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>   \
            event_engine) override;                                            \
                                                                               \
   private:                                                                    \
    static grpc_core::TransportFixture* Create() {                             \
      return new TransportFixture_##name();                                    \
    }                                                                          \
    static int registered_;                                                    \
  };                                                                           \
  int TransportFixture_##name::registered_ =                                   \
      (grpc_core::TransportFixtureRegistry::Get().RegisterFixture(             \
           #name, &TransportFixture_##name::Create),                           \
       0);                                                                     \
  grpc_core::TransportFixture::ClientAndServerTransportPair                    \
      TransportFixture_##name::CreateTransportPair(                            \
          std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine> \
              event_engine GRPC_UNUSED)

#endif  // GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_FIXTURE_H
