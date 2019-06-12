//
//  GTMNSColor+LuminanceTest.m
//
//  Copyright 2006-2009 Google Inc.
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

#import <Cocoa/Cocoa.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

#import "GTMSenTestCase.h"
#import "GTMNSColor+Luminance.h"

@interface GTMNSColor_LuminanceTest : GTMTestCase
@end

@implementation GTMNSColor_LuminanceTest

- (void)testLuminance {
  NSColor *midtone = [NSColor blueColor];
  NSColor *darker = [midtone gtm_colorAdjustedFor:GTMColorationBaseShadow];
  NSColor *lighter = [midtone gtm_colorAdjustedFor:GTMColorationBaseHighlight];
  NSColor *lightest = [midtone gtm_colorAdjustedFor:GTMColorationLightHighlight];
  NSColor *darkest = [midtone gtm_colorAdjustedFor:GTMColorationDarkShadow];

  // The relationships of the other values are not set, so we don't test them yet
  XCTAssertGreaterThanOrEqual([lightest gtm_luminance], [lighter gtm_luminance]);
  XCTAssertGreaterThanOrEqual([lighter gtm_luminance], [midtone gtm_luminance]);
  XCTAssertGreaterThanOrEqual([midtone gtm_luminance], [darker gtm_luminance]);
  XCTAssertGreaterThanOrEqual([darker gtm_luminance], [darkest gtm_luminance]);
  XCTAssertGreaterThanOrEqual([[NSColor whiteColor] gtm_luminance], (CGFloat)0.95);
  XCTAssertGreaterThanOrEqual([[NSColor yellowColor] gtm_luminance], (CGFloat)0.90);
  XCTAssertEqualWithAccuracy([[NSColor blueColor] gtm_luminance],
                             (CGFloat)0.35, 0.10);
  XCTAssertEqualWithAccuracy([[NSColor redColor] gtm_luminance],
                             (CGFloat)0.50, 0.10);
  XCTAssertLessThanOrEqual([[NSColor blackColor] gtm_luminance],
                           (CGFloat)0.30);
  XCTAssertTrue([[NSColor blackColor] gtm_isDarkColor]);
  XCTAssertTrue([[NSColor blueColor] gtm_isDarkColor]);
  XCTAssertTrue([[NSColor redColor] gtm_isDarkColor]);
  XCTAssertTrue(![[NSColor whiteColor] gtm_isDarkColor]);
  XCTAssertTrue(![[NSColor yellowColor] gtm_isDarkColor]);
  XCTAssertGreaterThanOrEqual([[[NSColor blackColor] gtm_legibleTextColor]
                                gtm_luminance],
                              [[NSColor grayColor] gtm_luminance]);
  XCTAssertLessThanOrEqual([[[NSColor whiteColor] gtm_legibleTextColor]
                            gtm_luminance],
                           [[NSColor grayColor] gtm_luminance]);
}

@end

#endif // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
