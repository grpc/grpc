//
//  GTMTimeUtilsTest.m
//
//  Copyright 2018 Google LLC
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
#import "GTMTimeUtils.h"

@interface GTMTimeUtilsTest : GTMTestCase
@end

@implementation GTMTimeUtilsTest

- (void)testAppLaunchDate {
  // Basic test to verify that "now" is after appLaunch.
  NSDate *now = [NSDate date];
  NSDate *appLaunch = GTMAppLaunchDate();

  XCTAssertEqual([now compare:appLaunch], NSOrderedDescending,
                 @"now: %@ appLaunch: %@", now, appLaunch);
}

- (void)testBootDate {
  // Basic test to verify that appLaunch occurred after boot.
  NSDate *appLaunch = GTMAppLaunchDate();
  NSDate *boot = GTMBootDate();

  XCTAssertEqual([appLaunch compare:boot], NSOrderedDescending,
                 @"appLaunch: %@ boot: %@", appLaunch, boot);
}

@end
