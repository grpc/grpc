//
//  GTMTestTimerTest.m
//
//  Copyright 2008 Google Inc.
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
#import "GTMTestTimer.h"

@interface GTMTestTimerTest : GTMTestCase
@end

@implementation GTMTestTimerTest
- (void)testTimer {
  GTMTestTimer *timer = GTMTestTimerCreate();
  XCTAssertNotNULL(timer);
  GTMTestTimerRetain(timer);
  GTMTestTimerRelease(timer);
  XCTAssertEqualWithAccuracy(GTMTestTimerGetSeconds(timer), 0.0, 0.0);
  GTMTestTimerStart(timer);
  XCTAssertTrue(GTMTestTimerIsRunning(timer));
  NSRunLoop *loop = [NSRunLoop currentRunLoop];
  [loop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  GTMTestTimerStop(timer);

  // We use greater than (and an almost absurd less than) because
  // these tests are very dependant on machine load, and we don't want
  // automated tests reporting false negatives.
  XCTAssertGreaterThan(GTMTestTimerGetSeconds(timer), 0.1);
  XCTAssertGreaterThan(GTMTestTimerGetMilliseconds(timer), 100.0);
  XCTAssertGreaterThan(GTMTestTimerGetMicroseconds(timer), 100000.0);

  // Check to make sure we're not WAY off the mark (by a factor of 10)
  XCTAssertLessThan(GTMTestTimerGetMicroseconds(timer), 1000000.0);

  [loop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  GTMTestTimerStart(timer);
  [loop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  XCTAssertGreaterThan(GTMTestTimerGetSeconds(timer), 0.2);
  GTMTestTimerStop(timer);
  XCTAssertEqual(GTMTestTimerGetIterations(timer), (NSUInteger)2);
  GTMTestTimerRelease(timer);
}
@end
