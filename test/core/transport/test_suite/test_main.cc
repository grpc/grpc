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

#include "absl/random/random.h"

#include "src/core/lib/debug/trace.h"
#include "test/core/transport/test_suite/fixture.h"
#include "test/core/transport/test_suite/test.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  absl::BitGen bitgen;
  ::testing::InitGoogleTest(&argc, argv);
  for (const auto& test : grpc_core::TransportTestRegistry::Get().tests()) {
    for (const auto& fixture :
         grpc_core::TransportFixtureRegistry::Get().fixtures()) {
      ::testing::RegisterTest(
          "TransportTest", absl::StrCat(test.name, "/", fixture.name).c_str(),
          nullptr, nullptr, __FILE__, __LINE__,
          [test = &test, fixture = &fixture,
           &bitgen]() -> grpc_core::TransportTest* {
            return test->create(
                std::unique_ptr<grpc_core::TransportFixture>(fixture->create()),
                fuzzing_event_engine::Actions(), bitgen);
          });
    }
  }
  grpc_tracer_init();
  return RUN_ALL_TESTS();
}
