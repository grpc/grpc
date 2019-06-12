/* Copyright (c) 2016 Google Inc.
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
 */

#import <XCTest/XCTest.h>

#import <objc/runtime.h>

// For testing, force use of the default debug versions of the _GTMDevAssert macro.
#undef _GTMDevAssert
#undef NS_BLOCK_ASSERTIONS
#undef DEBUG
#define DEBUG 1

#import "GTMSynchronizationAsserts.h"

@interface GTMSynchonizationAssertsTest : XCTestCase
@end

@implementation GTMSynchonizationAssertsTest

- (void)verifySynchronized {
  // Test both GTMCheckSynchronized and GTMCheckNotSynchronized assuming we're in a sync block.
  @try {
    GTMCheckSynchronized(self);
  } @catch (NSException *exception) {
    XCTFail(@"shouldn't have thrown");
  }

  @try {
    GTMCheckNotSynchronized(self);
    XCTFail(@"should have thrown");
  } @catch (NSException *exception) {
  }
}

- (void)verifyNotSynchronized {
  // Test both GTMCheckSynchronized and GTMCheckNotSynchronized assuming we're not in a sync block.
  @try {
    GTMCheckNotSynchronized(self);
  } @catch (NSException *exception) {
    XCTFail(@"shouldn't have thrown");
  }

  @try {
    GTMCheckSynchronized(self);
    XCTFail(@"shoul have thrown");
  } @catch (NSException *exception) {
  }
}

- (void)testChecks_SingleMethod {
  [self verifyNotSynchronized];

  @synchronized(self) {
    GTMMonitorSynchronized(self);
    [self verifySynchronized];

    @synchronized(self) {
      GTMMonitorRecursiveSynchronized(self);
      [self verifySynchronized];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [self verifySynchronized];
      }
    }
  }
  [self verifyNotSynchronized];
}

- (void)testChecks_AcrossMethods {
  [self doIndirectCheckNotSynchronized];

  @synchronized(self) {
    GTMMonitorSynchronized(self);
    [self verifySynchronized];

    @synchronized(self) {
      GTMMonitorRecursiveSynchronized(self);
      [self doIndirectCheckSynchronized];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [self doIndirectCheckSynchronized];
      }
    }
  }
  [self doIndirectCheckNotSynchronized];
}

- (void)doIndirectCheckSynchronized {
  // Verify from a separate method.
  [self verifySynchronized];
}

- (void)doIndirectCheckNotSynchronized {
  // Verify from a separate method.
  [self verifyNotSynchronized];
}

#pragma mark Sync Monitor Tests

- (void)testNonrecursiveSync {
  // Non-recursive monitors.
  XCTestExpectation *outer = [self expectationWithDescription:@"outer"];
  XCTestExpectation *inner = [self expectationWithDescription:@"inner"];

  @try {
    @synchronized(self) {
      GTMMonitorSynchronized(self);
      [outer fulfill];

      @synchronized(self) {
        GTMMonitorSynchronized(self);
        XCTFail(@"should have thrown");
      }
    }
  } @catch (NSException *exception) {
    [inner fulfill];
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

- (void)testRecursiveSync_SingleMethod {
  // The inner monitors are recursive.
  XCTestExpectation *outer = [self expectationWithDescription:@"outer"];
  XCTestExpectation *inner1 = [self expectationWithDescription:@"inner1"];
  XCTestExpectation *inner2 = [self expectationWithDescription:@"inner2"];

  @try {
    @synchronized(self) {
      GTMMonitorSynchronized(self);
      [outer fulfill];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [inner1 fulfill];

        @synchronized(self) {
          GTMMonitorRecursiveSynchronized(self);
          [inner2 fulfill];
        }
      }
    }
  } @catch (NSException *exception) {
    XCTFail(@"shouldn't have thrown");
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

- (void)testRecursiveSync_AcrossMethods {
  // The inner monitors are recursive.
  XCTestExpectation *outer = [self expectationWithDescription:@"outer"];
  XCTestExpectation *inner1 = [self expectationWithDescription:@"inner1"];
  XCTestExpectation *inner2 = [self expectationWithDescription:@"inner2"];

  @try {
    @synchronized(self) {
      GTMMonitorSynchronized(self);
      [outer fulfill];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [inner1 fulfill];

        [self doInnerRecursiveSync];
        [inner2 fulfill];
      }
    }
  } @catch (NSException *exception) {
    XCTFail(@"shouldn't have thrown");
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

- (void)doInnerRecursiveSync {
  @synchronized(self) {
    GTMMonitorRecursiveSynchronized(self);
  }
}

- (void)testRecursiveThenNonrecursiveSync_SingleMethod {
  // The outer monitors are recursive, but the inner one is not and should throw.
  XCTestExpectation *outer1 = [self expectationWithDescription:@"outer1"];
  XCTestExpectation *outer2 = [self expectationWithDescription:@"outer2"];
  XCTestExpectation *inner = [self expectationWithDescription:@"inner"];

  @try {
    @synchronized(self) {
      GTMMonitorRecursiveSynchronized(self);
      [outer1 fulfill];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [outer2 fulfill];

        @synchronized(self) {
          GTMMonitorSynchronized(self);
          XCTFail(@"should have thrown");
        }
      }
    }
  } @catch (NSException *exception) {
    [inner fulfill];
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

- (void)testRecursiveThenNonrecursiveSync_AcrossMethods {
  // The outer monitors are recursive, but the inner one is not and should throw.
  XCTestExpectation *outer1 = [self expectationWithDescription:@"outer1"];
  XCTestExpectation *outer2 = [self expectationWithDescription:@"outer2"];
  XCTestExpectation *inner = [self expectationWithDescription:@"inner"];

  @try {
    @synchronized(self) {
      GTMMonitorRecursiveSynchronized(self);
      [outer1 fulfill];

      @synchronized(self) {
        GTMMonitorRecursiveSynchronized(self);
        [outer2 fulfill];

        [self doInnerNonrecursiveSync];
      }
    }
  } @catch (NSException *exception) {
    [inner fulfill];
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

- (void)doInnerNonrecursiveSync {
  @synchronized(self) {
    GTMMonitorSynchronized(self);
    XCTFail(@"should have thrown");
  }
}

- (void)testSyncOnSeparateObjects {
  // Verify that monitoring works for distinct sync objects.
  XCTestExpectation *outer = [self expectationWithDescription:@"outer"];
  XCTestExpectation *innerA = [self expectationWithDescription:@"innerA"];
  XCTestExpectation *innerB = [self expectationWithDescription:@"innerB"];

  id obj1 = [[NSObject alloc] init];
  id obj2 = [[NSObject alloc] init];

  @try {
    @synchronized(obj1) {
      GTMMonitorSynchronized(obj1);
      [outer fulfill];

      @synchronized(obj2) {
        GTMMonitorSynchronized(obj2);
        [innerA fulfill];

        @synchronized(obj1) {
          GTMMonitorSynchronized(obj1);
          XCTFail(@"should have thrown");
        }
      }
    }
  } @catch (NSException *exception) {
    [innerB fulfill];
  }

  [self waitForExpectationsWithTimeout:1 handler:nil];
}

@end
