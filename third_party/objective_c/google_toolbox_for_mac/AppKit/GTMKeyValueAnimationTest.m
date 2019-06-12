//
//  GTMKeyValueAnimationTest.m
//
//  Copyright 2011 Google Inc.
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

#import "GTMSenTestCase.h"
#import "GTMKeyValueAnimation.h"

@interface GTMKeyValueAnimationTest : GTMTestCase <NSAnimationDelegate> {
 @private
  XCTestExpectation *oggleExpectation_;
  XCTestExpectation *shouldStartExpectation_;
}
@end

@implementation GTMKeyValueAnimationTest

- (void)testAnimation {
  GTMKeyValueAnimation *anim =
    [[[GTMKeyValueAnimation alloc] initWithTarget:self
                                          keyPath:@"oggle"] autorelease];
  oggleExpectation_ = [self expectationWithDescription:@"oggle"];
  // We are going to get called multiple times.
  oggleExpectation_.assertForOverFulfill = NO;
  shouldStartExpectation_ = [self expectationWithDescription:@"shouldStart"];
  [anim setDelegate:self];
  [anim startAnimation];
  [self waitForExpectationsWithTimeout:60 handler:NULL];
  [anim stopAnimation];
}

- (BOOL)animationShouldStart:(NSAnimation*)animation {
  [shouldStartExpectation_ fulfill];
  return YES;
}

- (void)setOggle:(CGFloat)oggle {
  [oggleExpectation_ fulfill];
}

@end
