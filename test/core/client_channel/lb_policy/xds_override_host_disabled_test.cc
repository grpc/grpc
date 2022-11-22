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

#include <stddef.h>

#include "src/core/lib/gprpp/env.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class XdsOverrideHostDisabledTest : public LoadBalancingPolicyTest {
 protected:
  XdsOverrideHostDisabledTest()
      : policy_(MakeLbPolicy("xds_override_host_experimental")) {}

  OrphanablePtr<LoadBalancingPolicy> policy_;
};

TEST_F(XdsOverrideHostDisabledTest, NoPolicyAvailable) {
  ASSERT_EQ(policy_, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto original_env_value =
      grpc_core::GetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE");
  grpc_core::UnsetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE");
  grpc_init();
  int ret = RUN_ALL_TESTS();
  if (original_env_value.has_value()) {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE",
                      *original_env_value);
  } else {
    grpc_core::UnsetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE");
  }
  grpc_shutdown();
  return ret;
}
