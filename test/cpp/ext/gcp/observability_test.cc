//
// Copyright 2022 gRPC authors.
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
//

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpcpp/ext/gcp_observability.h>

#include "src/core/lib/config/core_configuration.h"
#include "test/core/util/test_config.h"

namespace {

TEST(GcpObservabilityTest, RegistrationTest) {
  auto status = grpc::experimental::GcpObservabilityInit();
  EXPECT_EQ(status,
            absl::FailedPreconditionError(
                "Environment variables GRPC_GCP_OBSERVABILITY_CONFIG_FILE or "
                "GRPC_GCP_OBSERVABILITY_CONFIG "
                "not defined"));

  grpc_core::CoreConfiguration::Reset();
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
