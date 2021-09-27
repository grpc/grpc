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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/sync.h"

class EventEngineTest : public ::testing::Test {
 protected:
  /// To exercise a custom EventEngine, simply link against
  /// `:event_engine_test_suite` and provide a definition of
  /// `EventEnigneTest::NewEventEventEngine`. See README.md for details.
  std::unique_ptr<grpc_event_engine::experimental::EventEngine>
  NewEventEngine();
};

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_TEST_SUITE_EVENT_ENGINE_TEST_H
