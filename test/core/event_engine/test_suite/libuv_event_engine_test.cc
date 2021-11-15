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
// limitations under the License.#include <gtest/gtest.h>

#include "src/core/lib/event_engine/uv/libuv_event_engine.h"

#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/uv/libuv_event_engine.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"
#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::LibuvEventEngine;

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  SetEventEngineFactory(LibuvEventEngine::Create);
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
