//
//
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
//
//

#include "src/cpp/ext/gcp/environment_autodetect.h"

#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/notification.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

namespace {

class EnvironmentAutoDetectTest : public ::testing::Test {
 protected:
  void GetNotifiedOnEnvironmentDetection(
      grpc::internal::EnvironmentAutoDetect* env,
      grpc_core::Notification* notify) {
    env->NotifyOnDone([notify]() { notify->Notify(); });
  }
};

// TODO(yashykt): We could create a mock MetadataServer to test this more end to
// end, but given that that should be covered by our integration testing so
// deferring to that.

TEST_F(EnvironmentAutoDetectTest, Basic) {
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  // Unless we test in a specific GCP resource, we should get "global" here.
  // EXPECT_EQ(env.resource()->resource_type, "global");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
}

TEST_F(EnvironmentAutoDetectTest, GkeEnvironment) {
  grpc_core::SetEnv("KUBERNETES_SERVICE_HOST", "k8s_service_host");
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  EXPECT_EQ(env.resource()->resource_type, "k8s_container");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
  grpc_core::UnsetEnv("KUBERNETES_SERVICE_HOST");
}

TEST_F(EnvironmentAutoDetectTest, CloudFunctions) {
  grpc_core::SetEnv("FUNCTION_NAME", "function_name");
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  EXPECT_EQ(env.resource()->resource_type, "cloud_function");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
  grpc_core::UnsetEnv("FUNCTION_NAME");
}

TEST_F(EnvironmentAutoDetectTest, CloudRun) {
  grpc_core::SetEnv("K_CONFIGURATION", "config");
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  EXPECT_EQ(env.resource()->resource_type, "cloud_run_revision");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
  grpc_core::UnsetEnv("K_CONFIGURATION");
}

TEST_F(EnvironmentAutoDetectTest, AppEngine) {
  grpc_core::SetEnv("K_CONFIGURATION", "config");
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  EXPECT_EQ(env.resource()->resource_type, "cloud_run_revision");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
  grpc_core::UnsetEnv("K_CONFIGURATION");
}

TEST_F(EnvironmentAutoDetectTest, MultipleNotifyWaiters) {
  grpc::internal::EnvironmentAutoDetect env("project");

  grpc_core::Notification notify[10];
  for (int i = 0; i < 10; ++i) {
    GetNotifiedOnEnvironmentDetection(&env, &notify[i]);
  }
  for (int i = 0; i < 10; ++i) {
    notify[i].WaitForNotification();
  }

  // Unless we test in a specific GCP resource, we should get "global" here.
  // EXPECT_EQ(env.resource()->resource_type, "global");
  EXPECT_EQ((env.resource()->labels).at("project_id"), "project");
}

}  // namespace

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret_val = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret_val;
}
