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

// Regression tests for use-after-free races in Epoll1Poller::Work().
//
// Original bug: Work() collected pending Epoll1EventHandle* pointers under
// the poller mutex, then released the mutex before draining them via
// ExecutePendingActions().  A concurrent teardown (e.g. a c-ares DNS socket
// being orphaned via ~GrpcPolledFdPosix -> OrphanHandle() while the
// EventEngine/poller is torn down during fork) could return that handle to
// the free list and delete it (Epoll1Poller::Close()) inside that window.
// The freed slot was then recycled for an unrelated allocation -- in
// production a c-ares "address lookup failed for ..." std::string -- and the
// poller dereferenced the recycled bytes as a closure pointer -> SIGSEGV.
//
// Fix is scoped to Epoll1EventHandle synchronization, not the poller lock:
//  1. ExecutePendingActions() now takes handle->mu_, serializing with
//     OrphanHandle's DestroyEvent() and ShutdownHandle's SetShutdown().
//  2. OrphanHandle() resets pending_*_ atomics inside mu_ so the reset
//     happens atomically with DestroyEvent().
//  3. Close() no longer deletes free-listed handles; deletion is deferred
//     to ~Epoll1Poller(), which only runs when the last shared_ptr is gone
//     and no concurrent Work() can be in flight.
//  4. Work() keeps the closed_ check (under mu_) to skip stale events left
//     in g_epoll_set_ by a prior DoEpollWait() that ran before Close().
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

// Verifies that ExecutePendingActions() on a handle that is concurrently
// orphaned by another thread does not race with DestroyEvent() or access
// freed memory.  With the fix, handle->mu_ serializes the two, and Close()
// no longer frees handles so the drain always touches live memory.
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
    // c-ares DNS socket handle (returning it to the free list) and the
    // poller is then closed.  OrphanHandle takes handle->mu_ for
    // DestroyEvent; ExecutePendingActions on the poller thread takes the
    // same lock, so the two serialize.  Close() no longer deletes handles,
    // so even if it completes first the handle memory stays valid.
    handle->OrphanHandle(/*on_done=*/nullptr, /*release_fd=*/nullptr,
                         "test teardown");
    poller->Close();
    teardown_done.Notify();
  });

  // Drive a single Work() iteration. The schedule_poll_again callback runs
  // after pending events have been collected (and mu_ released); we use it
  // to let the teardown thread run concurrently with the drain.  With the
  // fix, ExecutePendingActions takes handle->mu_, serializing with
  // OrphanHandle's DestroyEvent.  Close() does not free the handle, so the
  // drain always touches live memory.  Without the fix, the drain races
  // with DestroyEvent on the same LockfreeEvent and the handle may be freed.
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

// Verifies that Work() bails out when Close() ran between a prior
// DoEpollWait() collecting events and the current mu_ acquisition.
//
// Bug: DoEpollWait() runs outside mu_.  Between epoll_wait() returning
// (with handle pointers stored in g_epoll_set_.events[]) and mu_ being
// acquired, a concurrent Close() can run.  ProcessEpollEvents() would then
// dereference stale event handle pointers and call SetPendingActions on
// handles whose LockfreeEvents have been DestroyEvent'd -- incorrect even
// though the memory is still valid (Close() no longer frees handles).
//
// This test exploits MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION == 1: the
// first Work() collects two events from epoll_wait() but processes only
// one, leaving the other in g_epoll_set_.events[].  After orphaning both
// handles and Close()ing the poller, the second Work() finds cursor <
// num_events (so DoEpollWait is skipped), acquires mu_, and must bail out
// via the closed_ check before ProcessEpollEvents() touches the stale
// event.
TEST(Epoll1PollerHandleLifetimeTest, WorkSkipsStaleEventsAfterClose) {
  auto thread_pool = std::make_shared<TestThreadPool>();
  std::shared_ptr<Epoll1Poller> poller = MakeEpoll1Poller(thread_pool);
  if (poller == nullptr) {
    GTEST_SKIP() << "epoll1 poller is not supported on this platform";
  }

  // Two pipe read-ends, both made readable so that a single epoll_wait()
  // returns both events.
  int pipefds_a[2];
  int pipefds_b[2];
  ASSERT_EQ(pipe(pipefds_a), 0);
  ASSERT_EQ(pipe(pipefds_b), 0);
  for (int* pf : {pipefds_a, pipefds_b}) {
    int flags = fcntl(pf[0], F_GETFL, 0);
    ASSERT_EQ(fcntl(pf[0], F_SETFL, flags | O_NONBLOCK), 0);
  }

  EventHandle* handle_a = poller->CreateHandle(
      poller->posix_interface().Adopt(pipefds_a[0]), "test-a",
      /*track_err=*/false);
  EventHandle* handle_b = poller->CreateHandle(
      poller->posix_interface().Adopt(pipefds_b[0]), "test-b",
      /*track_err=*/false);

  // Register read closures so SetReady() has something to schedule.
  handle_a->NotifyOnRead(PosixEngineClosure::TestOnlyToClosure(
      [](absl::Status /*status*/) {}));
  handle_b->NotifyOnRead(PosixEngineClosure::TestOnlyToClosure(
      [](absl::Status /*status*/) {}));

  // Make both readable so epoll_wait() returns both in one call.
  const char byte = 'x';
  ASSERT_EQ(write(pipefds_a[1], &byte, 1), 1);
  ASSERT_EQ(write(pipefds_b[1], &byte, 1), 1);

  // First Work(): DoEpollWait collects 2 events; ProcessEpollEvents
  // processes only 1 (MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION == 1),
  // leaving the other in g_epoll_set_.events[].
  auto result = poller->Work(5s, []() {});
  (void)result;

  // Orphan both handles and Close the poller.  Close() marks the poller
  // closed but no longer frees handles (deferred to ~Epoll1Poller).
  handle_a->OrphanHandle(/*on_done=*/nullptr, /*release_fd=*/nullptr,
                         "test teardown");
  handle_b->OrphanHandle(/*on_done=*/nullptr, /*release_fd=*/nullptr,
                         "test teardown");
  poller->Close();

  // Second Work(): cursor(1) < num_events(2), so DoEpollWait is skipped.
  // Work() acquires mu_ and must check closed_ before ProcessEpollEvents()
  // dereferences the stale event on a destroyed handle.  With the fix it
  // returns kKicked.  Without the fix, ProcessEpollEvents calls
  // SetPendingActions on a handle whose LockfreeEvents are destroyed.
  auto result2 = poller->Work(5s, []() {});
  EXPECT_EQ(result2, Poller::WorkResult::kKicked);

  // Cleanup.  read_fds were closed by OrphanHandle (release_fd == nullptr).
  close(pipefds_a[1]);
  close(pipefds_b[1]);
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
