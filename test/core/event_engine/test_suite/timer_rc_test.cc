// Copyright 2021 gRPC authors.
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

#include <random>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/functional/bind_front.h"
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::testing::ElementsAre;

class EventEngineTimerRCTest : public EventEngineTest {
 protected:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool signaled_ ABSL_GUARDED_BY(mu_) = false;
};

TEST_F(EventEngineTimerRCTest, DestroyedFromCallback) {
  // Helps transfer unique_ptr ownership to the callback.
  struct EngineOwner {
    std::unique_ptr<grpc_event_engine::experimental::EventEngine> engine;
  };
  EngineOwner* h = new EngineOwner{this->NewEventEngine()};
  // Destroy the engine from within an engine thread
  grpc_core::MutexLock lock(&mu_);
  signaled_ = false;
  h->engine->RunAt(absl::Now(), [this, h]() {
    ASSERT_TRUE(h->engine->IsWorkerThread());
    h->engine.reset();
    delete h;
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  while (!signaled_) {
    cv_.Wait(&mu_);
  }
}
