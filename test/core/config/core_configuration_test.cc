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

#include "src/core/lib/config/core_configuration.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace grpc_core {

// Allow substitution of config builder - in real code this would iterate
// through all plugins
namespace {
using ConfigBuilderFunction = std::function<void(CoreConfiguration::Builder*)>;
ConfigBuilderFunction g_mock_builder;
}  // namespace

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) {
  g_mock_builder(builder);
}

namespace {
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

TEST(ConfigTest, ThreadedInit) {
  CoreConfiguration::Reset();
  g_mock_builder = [](CoreConfiguration::Builder*) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  };
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; i++) {
    threads.push_back(std::thread([]() { CoreConfiguration::Get(); }));
  }
  for (auto& t : threads) {
    t.join();
  }
  g_mock_builder = nullptr;
  CoreConfiguration::Get();
}
}  // namespace

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
