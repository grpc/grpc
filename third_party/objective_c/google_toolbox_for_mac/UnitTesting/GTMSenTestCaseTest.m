//
//  GTMSenTestCaseTest.m
//
//  Copyright 2010 Google Inc.
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
//  License for the specific language governing permissions and limitations under
//  the License.
//

#import "GTMDefines.h"

// This test currently executes under XCTest and under the GTM SenTest replacement.
#if !GTM_USING_XCTEST
#define XCTAssertFalse STAssertFalse
#define XCTAssertEqual STAssertEquals
#define XCTAssertTrue STAssertTrue
#endif  // !GTM_USING_XCTEST

#import "GTMSenTestCase.h"

// These make use of the fact that methods are run in alphebetical order
// to have one test check that a previous one was run.  If that order ever
// changes, there is a good chance things will break.

static int gAbstractCalls_ = 0;
static int gZzCheckCalls_ = 0;

@interface GTMTestingAbstractTest : GTMTestCase
@end

@interface GTMTestingTestOne : GTMTestingAbstractTest {
  BOOL zzCheckCalled_;
}
@end

@interface GTMTestingTestTwo : GTMTestingTestOne
@end

@implementation GTMTestingAbstractTest

- (void)testAbstractUnitTest {
  XCTAssertFalse([self isMemberOfClass:[GTMTestingAbstractTest class]],
                 @"test should not run on the abstract class");
  ++gAbstractCalls_;
}

@end

@implementation GTMTestingTestOne

- (void)testZZCheck {
  ++gZzCheckCalls_;
  if ([self isMemberOfClass:[GTMTestingTestOne class]]) {
    XCTAssertEqual(gAbstractCalls_, 1,
                   @"wrong number of abstract calls at this point");
  } else {
    XCTAssertTrue([self isMemberOfClass:[GTMTestingTestTwo class]],
                  @"Not member of class");
    XCTAssertEqual(gAbstractCalls_, 2,
                   @"wrong number of abstract calls at this point");
  }
}

@end

@implementation GTMTestingTestTwo

- (void)testZZZCheck {
  // Test defined at this leaf, it should always run, check on the other methods.
  XCTAssertEqual(gZzCheckCalls_, 2, @"the parent class method wasn't called");
}

@end

@interface GTMSenTestCase  : GTMTestCase
@end

@implementation GTMSenTestCase
- (void)funcThatAsserts {
	NSAssert(nil, @"Should be nil");
}

- (void)testXCTAssertAsserts {
	XCTAssertAsserts([self funcThatAsserts]);
}

@end

