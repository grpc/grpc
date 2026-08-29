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

// Regression test for a use-after-free in Epoll1Poller::Work().
//
// Bug: Work() collected pending Epoll1EventHandle* pointers under the poller
// mutex, then released the mutex before draining them via
// ExecutePendingActions(). A concurrent teardown (e.g. a c-ares DNS socket
// being orphaned via ~GrpcPolledFdPosix -> OrphanHandle() while the
// EventEngine/poller is torn down during fork) could return that handle to the
// free list and delete it (Epoll1Poller::Close()) inside that window. The
// freed slot was then recycled for an unrelated allocation -- in production a
// c-ares "address lookup failed for ..." std::string -- and the poller
// dereferenced the recycled bytes as a closure pointer -> SIGSEGV.
//
// This test reproduces that exact interleaving deterministically: while a
// single Work() iteration is draining a readable handle, a helper thread
// orphans the handle and Close()s the poller (deleting it). With the bug, the
// drain dereferences freed memory; this is reliably caught under ASan/TSan and
// frequently crashes otherwise. With the fix (the drain runs under the poller
// mutex) the helper's teardown blocks until Work() completes, so no handle in
// `pending_events` can be freed while it is being dereferenced.
//
// Build: bazel test --config=asan
//        //test/core/event_engine/posix:epoll1_poller_handle_lifetime_test

#include <fcntl.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "src/core/lib/iomgr/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

#ifdef GRPC_LINUX_EPOLL

#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

namespace grpc_event_engine::experimental {
namespace {

using namespace std::chrono_literals;

// Verifies that a handle which is being drained by Work() cannot be orphaned
// and freed out from under the poller thread.
TEST(Epoll1PollerHandleLifetimeTest, DrainDoesNotUseFreedHandle) {
  auto thread_pool = std::make_shared<TestThreadPool>();
  std::shared_ptr<Epoll1Poller> poller = MakeEpoll1Poller(thread_pool);
  if (poller == nullptr) {
    GTEST_SKIP() << "epoll1 poller is not supported on this platform";
  }

  // A pipe whose read end we hand to the poller. It starts empty (not
  // readable), so registering it with the edge-triggered poller and then
  // writing a byte produces a clean not-ready -> ready transition that
  // epoll_wait() is guaranteed to report.
  int pipefds[2];
  ASSERT_EQ(pipe(pipefds), 0);
  int read_fd = pipefds[0];
  int write_fd = pipefds[1];
  int flags = fcntl(read_fd, F_GETFL, 0);
  ASSERT_EQ(fcntl(read_fd, F_SETFL, flags | O_NONBLOCK), 0);

  EventHandle* handle = poller->CreateHandle(
      poller->posix_interface().Adopt(read_fd), "test-fd", /*track_err=*/false);
  ASSERT_NE(handle, nullptr);

  grpc_core::Notification read_ran;
  handle->NotifyOnRead(PosixEngineClosure::TestOnlyToClosure(
      [&read_ran](absl::Status /*status*/) { read_ran.Notify(); }));

  // Make the read end readable so the upcoming Work() collects this handle
  // into its pending-events list.
  const char byte = 'x';
  ASSERT_EQ(write(write_fd, &byte, 1), 1);

  grpc_core::Notification teardown_go;
  grpc_core::Notification teardown_done;
  std::thread teardown([&]() {
    teardown_go.WaitForNotification();
    // This mirrors the production teardown: ~GrpcPolledFdPosix orphans the
    // c-ares DNS socket handle (returning it to the free list) and the poller
    // is then destroyed, deleting free-listed handles. Both take the poller
    // mutex.
    handle->OrphanHandle(/*on_done=*/nullptr, /*release_fd=*/nullptr,
                         "test teardown");
    poller->Close();
    // Encourage the freed handle slot to be recycled, mimicking the c-ares
    // error string that overwrote it in the production crash. This makes the
    // use-after-free more likely to manifest even without a sanitizer.
    std::vector<std::vector<char>> recycle;
    recycle.reserve(64);
    for (int i = 0; i < 64; ++i) {
      recycle.emplace_back(512, static_cast<char>(0xab));
    }
    teardown_done.Notify();
  });

  // Drive a single Work() iteration. The schedule_poll_again callback runs
  // after pending events have been collected; we use it to let the teardown
  // thread run concurrently with the drain. With the fix, Work() holds the
  // poller mutex across the drain, so OrphanHandle()/Close() block here until
  // Work() returns. With the bug, the handle is freed during the sleep and the
  // subsequent drain touches freed memory.
  auto result = poller->Work(5s, [&]() {
    teardown_go.Notify();
    absl::SleepFor(absl::Milliseconds(300 * grpc_test_slowdown_factor()));
  });
  (void)result;

  teardown_done.WaitForNotification();
  teardown.join();

  close(write_fd);
  // read_fd was closed by OrphanHandle (release_fd == nullptr). The poller was
  // already Close()d by the teardown thread; dropping it is a no-op.
  poller.reset();
}

}  // namespace
}  // namespace grpc_event_engine::experimental

#else  // GRPC_LINUX_EPOLL

TEST(Epoll1PollerHandleLifetimeTest, Skipped) {
  GTEST_SKIP() << "epoll1 is not available on this platform";
}

#endif  // GRPC_LINUX_EPOLL

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
