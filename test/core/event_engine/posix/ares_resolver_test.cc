// Copyright 2025 The gRPC Authors
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
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"

#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/posix_engine/grpc_polled_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"

namespace grpc_event_engine::experimental {

class AresResolverTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(AresInit().ok());
    event_engine_ =
        std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
    ASSERT_NE(event_engine_, nullptr);
  }

  void TearDown() override {
    factory_.reset();
    AresShutdown();
  }

  std::unique_ptr<GrpcPolledFdFactory> MakePolledFdFactory() {
    return std::make_unique<GrpcPolledFdFactoryPosix>(
        event_engine_->PollerForTests());
  }

  std::shared_ptr<PosixEventEngine> event_engine_;
  std::unique_ptr<GrpcPolledFdFactory> factory_;
};

TEST_F(AresResolverTest, ResolveGoogleCom) {
  auto resolver = event_engine_->GetDNSResolver({.dns_server = ""});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  ASSERT_NE(*resolver, nullptr);
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> lookup_result;
  grpc_core::Notification notification;
  resolver->get()->LookupHostname(
      [&](absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result) {
        lookup_result = std::move(result);
        notification.Notify();
      },
      "google.com", "80");
  notification.WaitForNotification();
  if (lookup_result.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << "Running in hermetic environment";
  }
  ASSERT_TRUE(lookup_result.ok()) << lookup_result.status();
  EXPECT_FALSE(lookup_result.value().empty());
}

#ifdef GRPC_ENABLE_FORK_SUPPORT

TEST_F(AresResolverTest, ForkSupportInParent) {
  auto resolver = event_engine_->GetDNSResolver({.dns_server = ""});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  ASSERT_NE(*resolver, nullptr);
  grpc_core::Notification notification;
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> lookup_result;
  // There should not be a LookupHostname calls between fork handlers in real
  // code. It is ok for unit test that is aware of actual implementation.
  // The trick here is that polling should be stopped so the callback is not
  // called before AfterFork
  event_engine_->BeforeFork();
  resolver->get()->LookupHostname(
      [&](absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result) {
        lookup_result = std::move(result);
        notification.Notify();
      },
      "youtube.com", "80");
  // Parent does not advance generation
  event_engine_->AfterFork(PosixEventEngine::OnForkRole::kParent);
  notification.WaitForNotification();
  if (lookup_result.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << "Running in hermetic environment";
  }
  ASSERT_TRUE(lookup_result.ok()) << lookup_result.status();
  EXPECT_FALSE(lookup_result.value().empty());
}

TEST_F(AresResolverTest, ForkSupportInChild) {
  auto resolver = event_engine_->GetDNSResolver({.dns_server = ""});
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  ASSERT_NE(*resolver, nullptr);
  grpc_core::Notification notification;
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> lookup_result;
  event_engine_->BeforeFork();
  resolver->get()->LookupHostname(
      [&](absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result) {
        lookup_result = std::move(result);
        notification.Notify();
      },
      "google.com", "80");
  // Child advances the generation
  event_engine_->AfterFork(PosixEventEngine::OnForkRole::kChild);
  notification.WaitForNotification();
  if (lookup_result.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << "Running in hermetic environment";
  }
  // This "unknown" error comes from the Ares library when the CAres channel is
  // closed.
  EXPECT_EQ(lookup_result.status().code(), absl::StatusCode::kUnknown)
      << lookup_result.status();
  grpc_core::Notification notification2;
  // Resolver should be reinitialized and ready for use.
  resolver->get()->LookupHostname(
      [&](absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result) {
        lookup_result = std::move(result);
        notification2.Notify();
      },
      "google.com", "80");
  notification2.WaitForNotification();
  if (lookup_result.status().code() == absl::StatusCode::kUnavailable) {
    GTEST_SKIP() << "Running in hermetic environment";
  }
  ASSERT_TRUE(lookup_result.ok()) << lookup_result.status();
  EXPECT_FALSE(lookup_result.value().empty());
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

}  // namespace grpc_event_engine::experimental

#else  // GRPC_ARES

TEST(AresResolverTest, Skipped) {
  GTEST_SKIP() << "Not a Posix platform or not using Ares resolver";
}

#endif  // GRPC_ARES

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}