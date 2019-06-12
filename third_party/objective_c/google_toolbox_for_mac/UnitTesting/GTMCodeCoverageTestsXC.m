//
//  GTMCodeCovereageTestsXC.m
//
//  Copyright 2013 Google Inc.
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

// This code exists for doing code coverage with Xcode and iOS.
// Please read through https://code.google.com/p/coverstory/wiki/UsingCoverstory
// for details.

// This file should be conditionally compiled into your test bundle
// when you want to do code coverage and are using the XCTest framework.

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>

#import "GTMCodeCoverageApp.h"

// Add GTM_IS_COVERAGE_BUILD to your GCC_PREPROCESSOR_DEFINITIONS for the
// Xcode Configuration that wants CodeCoverage support.
#if GTM_IS_COVERAGE_BUILD

extern void __gcov_flush();

// NOTE: As of Xcode 6, Apple made XCTestObserver and XCTestLog deprecated without
// having a replacement. Things still seem to work, but there doesn't seem to be a
// different way yet to hook when the tests finish.
// radr/18395261 - XCTestObserver deprecated with no replacement

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
@interface GTMCodeCoverageTests : XCTestObserver
@end
#pragma clang diagnostic pop

@implementation GTMCodeCoverageTests

- (void)stopObserving {
  [super stopObserving];

  // Call gtm_gcov_flush in the application executable unit.
  id application = [UIApplication sharedApplication];
  if ([application respondsToSelector:@selector(gtm_gcov_flush)]) {
    [application performSelector:@selector(gtm_gcov_flush)];
  }

  // Call flush for this executable unit.
  __gcov_flush();

  // Reset defaults back to what they should be.
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  [defaults removeObjectForKey:GTMXCTestObserverClassKey];
}

+ (void)load {
  // Verify that all of our assumptions in [GTMCodeCoverageApp load] still stand
  NSString *selfClass = NSStringFromClass(self);
  BOOL mustExit = NO;
  if (![selfClass isEqual:@"GTMCodeCoverageTests"]) {
    NSLog(@"Can't change GTMCodeCoverageTests name to %@ without updating GTMCoverageApp",
          selfClass);
    mustExit = YES;
  }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  if (![GTMXCTestObserverClassKey isEqual:XCTestObserverClassKey]) {
    NSLog(@"Apple has changed %@ to %@", GTMXCTestObserverClassKey, XCTestObserverClassKey);
    mustExit = YES;
  }
#pragma clang diagnostic pop
  if (!NSClassFromString(GTMXCTestLogClass)) {
    NSLog(@"Apple has gotten rid of the log class %@", GTMXCTestLogClass);
    mustExit = YES;
  }
  if (mustExit) {
    exit(1);
  }
}

@end

#endif  // GTM_IS_COVERAGE_BUILD
