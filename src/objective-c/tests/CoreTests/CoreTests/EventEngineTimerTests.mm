/*
 *
 * Copyright 2021 gRPC authors.
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

#import <XCTest/XCTest.h>

#include <absl/functional/bind_front.h>
#include <absl/time/time.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/test/core/util/test_config.h>

#include "src/core/lib/event_engine/uv/libuv_event_engine.h"

@interface EventEngineTimerTests : XCTestCase

@end

@implementation EventEngineTimerTests {
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool signaled_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<grpc_event_engine::experimental::EventEngine> _engine;
}

+ (void)setUp {
  grpc_init();
}

+ (void)tearDown {
  grpc_shutdown();
}

- (void)setUp {
  signaled_ = false;
  _engine = absl::make_unique<grpc_event_engine::experimental::LibuvEventEngine>();
}

- (void)tearDown {
  _engine = nullptr;
}

- (void)testImmediateCallbackIsExecutedQuickly {
  grpc_core::MutexLock lock(&mu_);
  _engine->RunAt(absl::Now(), [self]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
  XCTAssertTrue(signaled_);
}

- (void)testSupportsCancellation {
  auto handle = _engine->RunAt(absl::InfiniteFuture(), []() {});
  XCTAssertTrue(_engine->Cancel(handle));
}

- (void)testCancelledCallbackIsNotExecuted {
  {
    auto handle = _engine->RunAt(absl::InfiniteFuture(), [self]() {
      grpc_core::MutexLock lock(&mu_);
      signaled_ = true;
    });
    XCTAssertTrue(_engine->Cancel(handle));
    _engine = nullptr;
  }
  // The engine is deleted, and all closures should have been flushed
  grpc_core::MutexLock lock(&mu_);
  XCTAssertFalse(signaled_);
}

- (void)testTimersRespectScheduleOrdering {
  NSMutableArray<NSNumber *> *ordered = [NSMutableArray array];
  uint8_t count = 0;
  grpc_core::MutexLock lock(&mu_);
  {
    _engine->RunAt(absl::Now() + absl::Seconds(1), [&]() {
      grpc_core::MutexLock lock(&mu_);
      [ordered addObject:@(2)];
      ++count;
      cv_.Signal();
    });
    _engine->RunAt(absl::Now(), [&]() {
      grpc_core::MutexLock lock(&mu_);
      [ordered addObject:@(1)];
      ++count;
      cv_.Signal();
    });
    // Ensure both callbacks have run. Simpler than a mutex.
    while (count != 2) {
      cv_.WaitWithTimeout(&mu_, absl::Microseconds(100));
    }
    _engine = nullptr;
  }
  // The engine is deleted, and all closures should have been flushed beforehand
  NSArray *expected = @[ @(1), @(2) ];
  XCTAssertEqualObjects(ordered, expected);
}

- (void)testCancellingExecutedCallbackIsNoopAndReturnsFalse {
  grpc_core::MutexLock lock(&mu_);
  auto handle = _engine->RunAt(absl::Now(), [self]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(10));
  XCTAssertTrue(signaled_);
  // The callback has run, and now we'll try to cancel it.
  XCTAssertFalse(_engine->Cancel(handle));
}

- (void)testStressTestTimersNotCalledBeforeScheduled {
  // TODO import absl::random through cocoapod
}

@end
