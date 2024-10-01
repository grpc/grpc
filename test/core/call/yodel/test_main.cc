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
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/test_util/test_config.h"

int main(int argc, char** argv) {
  grpc_core::g_yodel_fuzzing = false;
  grpc::testing::TestEnvironment env(&argc, argv);
  absl::BitGen bitgen;
  ::testing::InitGoogleTest(&argc, argv);
  static grpc_core::NoDestruct<
      std::vector<grpc_core::yodel_detail::TestRegistry::Test>>
      tests{grpc_core::yodel_detail::TestRegistry::AllTests()};
  CHECK(!tests->empty());
  for (const auto& test : *tests) {
    CHECK(test.make != nullptr) << "test:" << test.name;
    ::testing::RegisterTest(
        test.test_type.c_str(), test.name.c_str(), nullptr, nullptr, __FILE__,
        __LINE__, [test = &test, &bitgen]() -> grpc_core::YodelTest* {
          return test->make(fuzzing_event_engine::Actions(), bitgen);
        });
  }
  grpc_tracer_init();
  return RUN_ALL_TESTS();
}
