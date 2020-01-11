/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/transport/connectivity_state.h"

#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tracer_util.h"

namespace grpc_core {
namespace {

TEST(ConnectivityStateName, Basic) {
  EXPECT_STREQ("IDLE", ConnectivityStateName(GRPC_CHANNEL_IDLE));
  EXPECT_STREQ("CONNECTING", ConnectivityStateName(GRPC_CHANNEL_CONNECTING));
  EXPECT_STREQ("READY", ConnectivityStateName(GRPC_CHANNEL_READY));
  EXPECT_STREQ("TRANSIENT_FAILURE",
               ConnectivityStateName(GRPC_CHANNEL_TRANSIENT_FAILURE));
  EXPECT_STREQ("SHUTDOWN", ConnectivityStateName(GRPC_CHANNEL_SHUTDOWN));
}

class Watcher : public ConnectivityStateWatcherInterface {
 public:
  Watcher(int* count, grpc_connectivity_state* output,
          bool* destroyed = nullptr)
      : count_(count), output_(output), destroyed_(destroyed) {}

  ~Watcher() {
    if (destroyed_ != nullptr) *destroyed_ = true;
  }

  void Notify(grpc_connectivity_state new_state) override {
    ++*count_;
    *output_ = new_state;
  }

 private:
  int* count_;
  grpc_connectivity_state* output_;
  bool* destroyed_;
};

TEST(StateTracker, SetAndGetState) {
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_CONNECTING);
  EXPECT_EQ(tracker.state(), GRPC_CHANNEL_CONNECTING);
  tracker.SetState(GRPC_CHANNEL_READY, "whee");
  EXPECT_EQ(tracker.state(), GRPC_CHANNEL_READY);
}

TEST(StateTracker, NotificationUponAddingWatcher) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_CONNECTING);
  tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                     MakeOrphanable<Watcher>(&count, &state));
  EXPECT_EQ(count, 1);
  EXPECT_EQ(state, GRPC_CHANNEL_CONNECTING);
}

TEST(StateTracker, NotificationUponStateChange) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_IDLE);
  tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                     MakeOrphanable<Watcher>(&count, &state));
  EXPECT_EQ(count, 0);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
  tracker.SetState(GRPC_CHANNEL_CONNECTING, "whee");
  EXPECT_EQ(count, 1);
  EXPECT_EQ(state, GRPC_CHANNEL_CONNECTING);
}

TEST(StateTracker, SubscribeThenUnsubscribe) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  bool destroyed = false;
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_IDLE);
  ConnectivityStateWatcherInterface* watcher =
      new Watcher(&count, &state, &destroyed);
  tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                     OrphanablePtr<ConnectivityStateWatcherInterface>(watcher));
  // No initial notification, since we started the watch from the
  // current state.
  EXPECT_EQ(count, 0);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
  // Cancel watch.  This should not generate another notification.
  tracker.RemoveWatcher(watcher);
  EXPECT_TRUE(destroyed);
  EXPECT_EQ(count, 0);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
}

TEST(StateTracker, OrphanUponShutdown) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  bool destroyed = false;
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_IDLE);
  ConnectivityStateWatcherInterface* watcher =
      new Watcher(&count, &state, &destroyed);
  tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                     OrphanablePtr<ConnectivityStateWatcherInterface>(watcher));
  // No initial notification, since we started the watch from the
  // current state.
  EXPECT_EQ(count, 0);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
  // Set state to SHUTDOWN.
  tracker.SetState(GRPC_CHANNEL_SHUTDOWN, "shutting down");
  EXPECT_TRUE(destroyed);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(state, GRPC_CHANNEL_SHUTDOWN);
}

TEST(StateTracker, AddWhenAlreadyShutdown) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  bool destroyed = false;
  ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_SHUTDOWN);
  ConnectivityStateWatcherInterface* watcher =
      new Watcher(&count, &state, &destroyed);
  tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                     OrphanablePtr<ConnectivityStateWatcherInterface>(watcher));
  EXPECT_TRUE(destroyed);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(state, GRPC_CHANNEL_SHUTDOWN);
}

TEST(StateTracker, NotifyShutdownAtDestruction) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  {
    ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_IDLE);
    tracker.AddWatcher(GRPC_CHANNEL_IDLE,
                       MakeOrphanable<Watcher>(&count, &state));
    // No initial notification, since we started the watch from the
    // current state.
    EXPECT_EQ(count, 0);
    EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
  }
  // Upon tracker destruction, we get a notification for SHUTDOWN.
  EXPECT_EQ(count, 1);
  EXPECT_EQ(state, GRPC_CHANNEL_SHUTDOWN);
}

TEST(StateTracker, DoNotNotifyShutdownAtDestructionIfAlreadyInShutdown) {
  int count = 0;
  grpc_connectivity_state state = GRPC_CHANNEL_SHUTDOWN;
  {
    ConnectivityStateTracker tracker("xxx", GRPC_CHANNEL_SHUTDOWN);
    tracker.AddWatcher(GRPC_CHANNEL_SHUTDOWN,
                       MakeOrphanable<Watcher>(&count, &state));
    // No initial notification, since we started the watch from the
    // current state.
    EXPECT_EQ(count, 0);
    EXPECT_EQ(state, GRPC_CHANNEL_SHUTDOWN);
  }
  // No additional notification upon tracker destruction, since we were
  // already in state SHUTDOWN.
  EXPECT_EQ(count, 0);
  EXPECT_EQ(state, GRPC_CHANNEL_SHUTDOWN);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  grpc_core::testing::grpc_tracer_enable_flag(
      &grpc_core::grpc_connectivity_state_trace);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
