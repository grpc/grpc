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

#include "src/cpp/ext/filters/census/environment_autodetect.h"

#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class EnvironmentAutoDetectTest : public ::testing::Test {
 protected:
  EnvironmentAutoDetectTest() {
    pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset_, &mu_);
    pollent_ = grpc_polling_entity_create_from_pollset(pollset_);
    // Start a thread for polling.
    poller_ = std::thread([&]() {
      while (!done_) {
        grpc_core::ExecCtx exec_ctx;
        grpc_pollset_worker* worker = nullptr;
        gpr_mu_lock(mu_);
        if (!GRPC_LOG_IF_ERROR(
                "pollset_work",
                grpc_pollset_work(grpc_polling_entity_pollset(&pollent_),
                                  &worker,
                                  grpc_core::Timestamp::Now() +
                                      grpc_core::Duration::Seconds(1)))) {
          done_ = true;
        }
        gpr_mu_unlock(mu_);
      }
    });
  }

  ~EnvironmentAutoDetectTest() override {
    grpc_core::ExecCtx exec_ctx;
    poller_.join();
    grpc_pollset_shutdown(
        pollset_, GRPC_CLOSURE_CREATE(
                      [](void* arg, absl::Status /* status */) {
                        grpc_pollset_destroy(static_cast<grpc_pollset*>(arg));
                        gpr_free(arg);
                      },
                      pollset_, nullptr));
  }

  void GetNotifiedOnEnvironmentDetection(
      grpc::internal::EnvironmentAutoDetect* env, absl::Notification* notify) {
    env->NotifyOnDone(&pollent_, [&]() {
      gpr_mu_lock(mu_);
      done_ = true;
      GRPC_LOG_IF_ERROR(
          "Pollset kick",
          grpc_pollset_kick(grpc_polling_entity_pollset(&pollent_), nullptr));
      gpr_mu_unlock(mu_);
      notify->Notify();
    });
    grpc_core::ExecCtx::Get()->Flush();
  }

  grpc_pollset* pollset_ = nullptr;
  gpr_mu* mu_ = nullptr;
  grpc_polling_entity pollent_;
  bool done_ = false;
  std::thread poller_;
};

TEST_F(EnvironmentAutoDetectTest, Basic) {
  grpc_core::ExecCtx exec_ctx;
  grpc::internal::EnvironmentAutoDetect env("project");
  EXPECT_TRUE(env.resource() == nullptr);

  absl::Notification notify;
  GetNotifiedOnEnvironmentDetection(&env, &notify);
  notify.WaitForNotification();

  // Unless we test in a specific GCP resource, we should get "global" here.
  EXPECT_EQ(env.resource()->resource_type, "global");
  EXPECT_THAT(env.resource()->labels,
              UnorderedElementsAre(Pair("project_id", "project")));
}

TEST_F(EnvironmentAutoDetectTest, MultipleNotifyWaiters) {
  grpc_core::ExecCtx exec_ctx;
  grpc::internal::EnvironmentAutoDetect env("project");
  EXPECT_TRUE(env.resource() == nullptr);

  absl::Notification notify[10];
  for (int i = 0; i < 10; ++i) {
    GetNotifiedOnEnvironmentDetection(&env, &notify[i]);
  }
  for (int i = 0; i < 10; ++i) {
    notify[i].WaitForNotification();
  }

  EXPECT_EQ(env.resource()->resource_type, "global");
  EXPECT_THAT(env.resource()->labels,
              UnorderedElementsAre(Pair("project_id", "project")));
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
