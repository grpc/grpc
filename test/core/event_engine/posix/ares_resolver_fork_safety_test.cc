// Copyright 2026 The gRPC Authors
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

// Regression test for SIGABRT in AresResolver::~AresResolver() when
// Reset() is called via ReinitHandle during fork, followed by resolver
// teardown without Restart().
//
// Bug: Reset() unconditionally destroys channel_ and sets it to nullptr.
// If the resolver is then orphaned (e.g. application tears down gRPC
// resources during fork), the destructor fires with channel_ == nullptr
// and hits GRPC_CHECK_NE(channel_, nullptr) → SIGABRT.
//
// This test verifies that Reset() followed by Orphan() (without Restart)
// does NOT crash.
//
// Build: bazel test --config=fork_support
//        //test/core/event_engine/posix:ares_resolver_fork_safety_test

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/posix_engine/grpc_polled_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/posix/dns_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_event_engine::experimental {

#if GRPC_ARES == 1
#ifdef GRPC_ENABLE_FORK_SUPPORT

// Named AresResolverTest to match PosixEventEngine's friend declaration,
// allowing access to the private poller_ member.
class AresResolverTest : public ::testing::Test {
 protected:
  static PosixEventPoller* GetPoller(PosixEventEngine* engine) {
    return engine->poller_.get();
  }
};

// Verifies that an AresResolver can be safely destroyed after Reset()
// has been called without a subsequent Restart().
//
// This simulates the production crash scenario where:
// 1. A fork handler calls Reset() on the resolver via ReinitHandle
//    (e.g. PosixEventEngine::AfterForkInChild)
// 2. The application tears down gRPC resources during fork, which
//    orphans the resolver before Restart() can be called
// 3. The resolver destructor should handle channel_ == nullptr gracefully
TEST_F(AresResolverTest, ResetThenOrphanDoesNotCrash) {
  auto ee = std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
  auto* poller = GetPoller(ee.get());

  auto ares_resolver = AresResolver::CreateAresResolver(
      "", std::make_unique<GrpcPolledFdFactoryPosix>(poller), ee);
  ASSERT_TRUE(ares_resolver.ok()) << ares_resolver.status();

  auto reinit_handle = ares_resolver->get()->GetReinitHandle().lock();
  ASSERT_NE(reinit_handle, nullptr);

  // Simulate fork handler: Reset destroys channel_ and sets to nullptr
  reinit_handle->Reset(absl::CancelledError("simulated fork reset"));

  // Simulate application teardown: Orphan the resolver without Restart.
  // Orphan() calls OnResolverGone() (makes future Restart() a no-op),
  // then Unref(). This triggers ~AresResolver() which should not crash
  // even though channel_ is nullptr.
  ares_resolver->reset();

  // If we get here, the resolver was safely destroyed. Clean up handle.
  reinit_handle.reset();
}

// Verifies that an AresResolver with in-flight DNS lookups can be safely
// destroyed after Reset() without Restart().
//
// This is a more realistic scenario: the resolver has active DNS queries
// when the fork handler fires and tears things down.
TEST_F(AresResolverTest, ResetThenOrphanWithPendingLookupsDoesNotCrash) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  ASSERT_TRUE(dns_server.ok()) << dns_server.status();

  auto ee = std::static_pointer_cast<PosixEventEngine>(GetDefaultEventEngine());
  auto* poller = GetPoller(ee.get());

  auto ares_resolver = AresResolver::CreateAresResolver(
      dns_server->address(), std::make_unique<GrpcPolledFdFactoryPosix>(poller),
      ee);
  ASSERT_TRUE(ares_resolver.ok()) << ares_resolver.status();

  auto reinit_handle = ares_resolver->get()->GetReinitHandle().lock();
  ASSERT_NE(reinit_handle, nullptr);

  // Start DNS lookups to create in-flight resolver state
  grpc_core::Notification lookup_done;
  ares_resolver->get()->LookupHostname(
      [&lookup_done](const auto& /*result*/) { lookup_done.Notify(); },
      "test.host.example.com", "443");

  // Wait for the query to reach the DNS server
  dns_server->WaitForQuestion("test.host.example.com");

  // Now simulate the crash scenario: Reset then Orphan
  reinit_handle->Reset(absl::CancelledError("simulated fork reset"));
  ares_resolver->reset();
  reinit_handle.reset();

  // Wait for the lookup callback (it was cancelled by Reset)
  lookup_done.WaitForNotification();
}

namespace {

struct FakePolledFdState {
  bool current = true;
  std::vector<absl::AnyInvocable<void(absl::Status)>> closures;
};

class FakePolledFd final : public GrpcPolledFd {
 public:
  FakePolledFd(ares_socket_t as, std::shared_ptr<FakePolledFdState> state)
      : as_(as), state_(std::move(state)) {}

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    state_->closures.push_back(std::move(read_closure));
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    state_->closures.push_back(std::move(write_closure));
  }

  bool IsFdStillReadableLocked() override { return false; }

  bool ShutdownLocked(absl::Status /*error*/) override { return true; }

  ares_socket_t GetWrappedAresSocketLocked() override { return as_; }

  const char* GetName() const override { return "fake c-ares fd"; }

  bool IsCurrent() const override { return state_->current; }

 private:
  ares_socket_t as_;
  std::shared_ptr<FakePolledFdState> state_;
};

class FakePolledFdFactory final : public GrpcPolledFdFactory {
 public:
  explicit FakePolledFdFactory(std::shared_ptr<FakePolledFdState> state)
      : state_(std::move(state)) {}

  void Initialize(grpc_core::Mutex* /*mutex*/,
                  EventEngine* /*event_engine*/) override {}

  std::unique_ptr<GrpcPolledFd> NewGrpcPolledFdLocked(
      ares_socket_t as) override {
    return std::make_unique<FakePolledFd>(as, state_);
  }

  void ConfigureAresChannelLocked(ares_channel /*channel*/) override {}

  std::unique_ptr<GrpcPolledFdFactory> NewEmptyInstance() const override {
    return std::make_unique<FakePolledFdFactory>(state_);
  }

 private:
  std::shared_ptr<FakePolledFdState> state_;
};

void RunFakePolledFdClosures(FakePolledFdState* state) {
  auto closures = std::move(state->closures);
  state->closures.clear();
  for (auto& closure : closures) {
    closure(absl::CancelledError("simulated stale fd shutdown"));
  }
}

}  // namespace

// Verifies that destroying an orphaned AresResolver with in-flight DNS lookups
// drains outstanding c-ares callbacks before checking callback_map_.
//
// This covers the teardown path where Orphan() shuts down poller FDs, but
// Reset() was not called first to clear callback_map_. In that case the
// destructor must let ares_destroy() synchronously invoke pending query
// callbacks so callback_map_ can drain before the final invariant check.
TEST_F(AresResolverTest, OrphanWithPendingLookupDrainsCallbacksDoesNotCrash) {
  auto dns_server = DnsServer::Start(grpc_pick_unused_port_or_die());
  ASSERT_TRUE(dns_server.ok()) << dns_server.status();

  auto ee = PosixEventEngine::MakePosixEventEngine();
  auto fd_state = std::make_shared<FakePolledFdState>();

  auto ares_resolver = AresResolver::CreateAresResolver(
      dns_server->address(), std::make_unique<FakePolledFdFactory>(fd_state),
      ee);
  ASSERT_TRUE(ares_resolver.ok()) << ares_resolver.status();

  grpc_core::Notification lookup_done;
  ares_resolver->get()->LookupHostname(
      [&lookup_done](const auto& result) {
        EXPECT_FALSE(result.ok()) << result.status();
        lookup_done.Notify();
      },
      "pending-destroy.example.com", "443");

  // Ensure c-ares has an outstanding query and callback_map_ is populated, but
  // do not answer it. The pending callback should be drained by ares_destroy()
  // when the resolver is eventually destroyed after Orphan().
  dns_server->WaitForQuestion("pending-destroy.example.com");
  ASSERT_FALSE(fd_state->closures.empty());

  // Simulate the fork-generation change from the production crash. When the
  // pending fd callbacks run after Orphan(), IsCurrent() returns false, so
  // OnReadable()/OnWritable() clean up fd_node_list_ without calling
  // ares_cancel(). That leaves callback_map_ populated until ares_destroy()
  // runs in the destructor.
  fd_state->current = false;
  ares_resolver->reset();
  RunFakePolledFdClosures(fd_state.get());

  // If the destructor checks callback_map_ before ares_destroy() drains the
  // outstanding query callbacks, this test will abort before notification.
  lookup_done.WaitForNotification();
}

#else  // GRPC_ENABLE_FORK_SUPPORT

TEST(AresResolverTest, Skipped) { GTEST_SKIP() << "Fork support is disabled"; }

#endif  // GRPC_ENABLE_FORK_SUPPORT
#else   // GRPC_ARES == 1

TEST(AresResolverTest, Skipped) { GTEST_SKIP() << "c-ares is disabled"; }

#endif  // GRPC_ARES == 1

}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
