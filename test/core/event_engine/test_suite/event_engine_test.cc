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
#include "test/core/event_engine/test_suite/event_engine_test.h"

#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>

std::function<std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>*
    g_ee_factory = nullptr;

std::function<std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>*
    g_oracle_ee_factory = nullptr;

void SetEventEngineFactories(
    std::function<
        std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
        factory,
    std::function<
        std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
        oracle_ee_factory) {
  testing::AddGlobalTestEnvironment(
      new EventEngineTestEnvironment(factory, oracle_ee_factory));
}
