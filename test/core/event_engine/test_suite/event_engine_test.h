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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_H
#define GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_H
#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/sync.h"

extern std::function<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>*
    g_ee_factory;

// Manages the lifetime of the global EventEngine factory.
class EventEngineTestEnvironment : public testing::Environment {
 public:
  explicit EventEngineTestEnvironment(
      std::function<
          std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
          factory)
      : factory_(factory) {}

  void SetUp() override { g_ee_factory = &factory_; }

  void TearDown() override { g_ee_factory = nullptr; }

 private:
  std::function<std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
      factory_;
};

class EventEngineTest : public testing::Test {
 protected:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine>
  NewEventEngine() {
    GPR_ASSERT(g_ee_factory != nullptr);
    return (*g_ee_factory)();
  }
};

// Set a custom factory for the EventEngine test suite.
void SetEventEngineFactory(
    std::function<
        std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
        factory);

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_H
