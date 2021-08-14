// Copyright 2021 gRPC authors.
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

#include "src/core/lib/config/config.h"
#include <gtest/gtest.h>

namespace grpc_core {

// Allow substitution of config builder - in real code this would iterate
// through all plugins
namespace testing {
using ConfigBuilderFunction = std::function<void(CoreConfiguration::Builder*)>;
static ConfigBuilderFunction g_mock_builder;
}

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) { ::grpc_core::testing::g_mock_builder(builder); }

namespace testing {
// Helper for testing - clear out any state, rebuild configuration with fn being
// the initializer
void InitConfigWithBuilder(ConfigBuilderFunction fn) {
  CoreConfiguration::Reset();
  g_mock_builder = fn;
  CoreConfiguration::Get();
  g_mock_builder = nullptr;
}

TEST(ConfigTest, NoopConfig) {
  InitConfigWithBuilder([](CoreConfiguration::Builder*) {});
  CoreConfiguration::Get();
}
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
