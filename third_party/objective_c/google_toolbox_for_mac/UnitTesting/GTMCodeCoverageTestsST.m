//
//  GTMCodeCoverageTestsST.m
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
// when you want to do code coverage and are using the SenTestingKit framework.

#import <UIKit/UIKit.h>
#import <SenTestingKit/SenTestingKit.h>

#import "GTMCodeCoverageApp.h"

// Add GTM_IS_COVERAGE_BUILD to your GCC_PREPROCESSOR_DEFINITIONS for the
// Xcode Configuration that wants CodeCoverage support.
#if GTM_IS_COVERAGE_BUILD

extern void __gcov_flush();

static int gSuiteCount = 0;

@interface GTMCodeCoverageTests : NSObject
@end

@implementation GTMCodeCoverageTests

+ (void)load {
  // Hook into the notifications so that we know when test suites start and
  // stop. Once gSuiteCount is back to 0 we know that all of the suites
  // have been run and we can collect our usage data.
  NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
  [nc addObserver:self
         selector:@selector(suiteStarted:)
             name:SenTestSuiteDidStartNotification
           object:nil];
  [nc addObserver:self
         selector:@selector(suiteStopped:)
             name:SenTestSuiteDidStopNotification
           object:nil];
}

+ (void)suiteStarted:(NSNotification *)notification {
  #pragma unused (notification)
  gSuiteCount += 1;
}

+ (void)suiteStopped:(NSNotification *)notification {
  #pragma unused (notification)
  gSuiteCount -= 1;
  if (gSuiteCount == 0) {
    id application = [UIApplication sharedApplication];
    if ([application respondsToSelector:@selector(gtm_gcov_flush)]) {
      [application performSelector:@selector(gtm_gcov_flush)];
    }

    // Call flush for this executable unit.
    __gcov_flush();
  }
}

@end

#endif  // GTM_IS_COVERAGE_BUILD
