// Copyright 2020 gRPC authors.
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

#include "src/core/lib/security/authorization/cel_evaluation_engine.h"

#include <gtest/gtest.h>

TEST(CelEngineTest, CreateEmptyEngine) {
  upb::Arena arena;
  envoy_config_rbac_v2_RBAC* rbac_policy =
      envoy_config_rbac_v2_RBAC_new(arena.ptr());
  CelEvaluationEngine* cel_engine = new CelEvaluationEngine(*rbac_policy);
  EXPECT_NE(cel_engine, nullptr)
      << "Failed to create an empty CelEvaluationEngine.";
  delete cel_engine;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}