//
//  GTMNSThread+BlocksTest.m
//
//  Copyright 2012 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations
//  under the License.
//

#import <pthread.h>

#import "GTMSenTestCase.h"
#import "GTMNSThread+Blocks.h"

static const NSTimeInterval kTestTimeout = 10;
static const int kThreadMethodCounter = 5;
static const int kThreadMethoduSleep = 10000;

@interface GTMNSThread_BlocksTest : GTMTestCase {
 @private
  GTMSimpleWorkerThread *workerThread_;
}
@end

@implementation GTMNSThread_BlocksTest

- (void)setUp {
  workerThread_ = [[GTMSimpleWorkerThread alloc] init];
  [workerThread_ start];
}

- (void)tearDown {
  [workerThread_ cancel];
  [workerThread_ release];
}

- (void)testPerformBlockOnCurrentThread {
  NSThread *currentThread = [NSThread currentThread];
  __block NSThread *runThread = nil;

  // Straight block runs right away (no runloop spin)
  [currentThread gtm_performBlock:^{
    runThread = [NSThread currentThread];
  }];
  XCTAssertEqualObjects(runThread, currentThread);

  // Block with waiting runs immediately as well.
  runThread = nil;
  [currentThread gtm_performWaitingUntilDone:YES block:^{
    runThread = [NSThread currentThread];
  }];
  XCTAssertEqualObjects(runThread, currentThread);

  // Block without waiting requires a runloop spin.
  runThread = nil;
  XCTestExpectation *expectation =
      [self expectationWithDescription:@"BlockRan"];
  [currentThread gtm_performWaitingUntilDone:NO block:^{
    runThread = [NSThread currentThread];
    [expectation fulfill];
  }];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertEqualObjects(runThread, currentThread);
}

- (void)testPerformBlockInBackground {
  XCTestExpectation *expectation =
      [self expectationWithDescription:@"BlockRan"];
  __block NSThread *runThread = nil;
  [NSThread gtm_performBlockInBackground:^{
    runThread = [NSThread currentThread];
    [expectation fulfill];
  }];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertNotNil(runThread);
  XCTAssertNotEqualObjects(runThread, [NSThread currentThread]);
}

- (void)testWorkerThreadBasics {
  // Unstarted worker isn't running.
  GTMSimpleWorkerThread *worker = [[GTMSimpleWorkerThread alloc] init];
  XCTAssertFalse([worker isExecuting]);
  XCTAssertFalse([worker isFinished]);


  // Unstarted worker can be cancelled without error.
  [worker cancel];
  XCTAssertFalse([worker isExecuting]);
  XCTAssertFalse([worker isFinished]);

  // And can be cancelled again
  [worker cancel];
  XCTAssertFalse([worker isExecuting]);
  XCTAssertFalse([worker isFinished]);
  [worker release];

  // A thread we start can be cancelled with correct state.
  worker = [[GTMSimpleWorkerThread alloc] init];
  XCTAssertFalse([worker isExecuting]);
  XCTAssertFalse([worker isFinished]);
  XCTestExpectation *blockPerformed =
      [self expectationWithDescription:@"BlockIsRunning"];
  [worker start];
  [workerThread_ gtm_performWaitingUntilDone:YES block:^{
    [blockPerformed fulfill];
  }];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([worker isExecuting]);
  XCTAssertFalse([worker isCancelled]);
  XCTAssertFalse([worker isFinished]);
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:worker
                        handler:NULL];

  [worker cancel];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertFalse([worker isExecuting]);
  XCTAssertTrue([worker isCancelled]);
  XCTAssertTrue([worker isFinished]);
  [worker release];
}

- (void)testPerformBlockOnWorkerThread {
  __block NSThread *runThread = nil;

  // Runs on the other thread
  XCTestExpectation *expectation =
      [self expectationWithDescription:@"BlockRan"];
  [workerThread_ gtm_performBlock:^{
    runThread = [NSThread currentThread];
    [expectation fulfill];
  }];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertNotNil(runThread);
  XCTAssertEqualObjects(runThread, workerThread_);

  // Other thread no wait.
  runThread = nil;
  expectation = [self expectationWithDescription:@"BlockRan2"];
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
    runThread = [NSThread currentThread];
    [expectation fulfill];
  }];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertNotNil(runThread);
  XCTAssertEqualObjects(runThread, workerThread_);

  // Waiting requires no runloop spin
  runThread = nil;
  [workerThread_ gtm_performWaitingUntilDone:YES block:^{
    runThread = [NSThread currentThread];
  }];
  XCTAssertNotNil(runThread);
  XCTAssertEqualObjects(runThread, workerThread_);
}

- (void)testExitingBlock {
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
     pthread_exit(NULL);
  }];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
}

- (void)testCancelFromThread {
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
    [workerThread_ cancel];
  }];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
}

- (void)testNestedCancelFromThread {
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      [workerThread_ gtm_performWaitingUntilDone:NO block:^{
        [workerThread_ gtm_performWaitingUntilDone:NO block:^{
          [workerThread_ cancel];
        }];
      }];
    }];
  }];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
}

- (void)testCancelFromOtherThread {
  // Cancel will kill the thread at same point.
  // It may or may not complete all the blocks.
  // There is no guarantee made (unlike stop).
  for (int i = 0; i < kThreadMethodCounter; i++) {
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      usleep(kThreadMethoduSleep);
    }];
  }
  [workerThread_ cancel];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
}

- (void)testStopFromThread {
  // Show that stop forces all blocks to be executed.
  __block int counter = 0;
  for (int i = 0; i < kThreadMethodCounter; i++) {
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      usleep(kThreadMethoduSleep);
      ++counter;
    }];
  }
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
    [workerThread_ stop];
  }];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
      return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
  XCTAssertEqual(counter, kThreadMethodCounter);
}

- (void)testNestedStopFromThread {
  __block int counter = 0;
  for (int i = 0; i < kThreadMethodCounter; i++) {
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      usleep(kThreadMethoduSleep);
      ++counter;
    }];
  }
  [workerThread_ gtm_performWaitingUntilDone:NO block:^{
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      [workerThread_ gtm_performWaitingUntilDone:NO block:^{
        [workerThread_ gtm_performWaitingUntilDone:NO block:^{
          [workerThread_ stop];
        }];
      }];
    }];
  }];
  NSPredicate *predicate =
      [NSPredicate predicateWithBlock:^BOOL(id workerThread,
                                            NSDictionary<NSString *,id> *opts) {
    return (BOOL)(![workerThread isExecuting]);
  }];
  [self expectationForPredicate:predicate
            evaluatedWithObject:workerThread_
                        handler:NULL];
  [self waitForExpectationsWithTimeout:kTestTimeout handler:NULL];
  XCTAssertTrue([workerThread_ isFinished]);
  XCTAssertEqual(counter, kThreadMethodCounter);
}

- (void)testStopFromOtherThread {
  __block int counter = 0;
  for (int i = 0; i < kThreadMethodCounter; i++) {
    [workerThread_ gtm_performWaitingUntilDone:NO block:^{
      usleep(kThreadMethoduSleep);
      ++counter;
    }];
  }
  [workerThread_ stop];
  XCTAssertTrue([workerThread_ isFinished]);
  XCTAssertEqual(counter, kThreadMethodCounter);
}

@end
