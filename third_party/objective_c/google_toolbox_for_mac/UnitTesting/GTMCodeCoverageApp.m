//
//  GTMCodeCovereageApp.m
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

// This file should be conditionally compiled into your application bundle
// or test rig when you want to do code coverage.

#import "GTMCodeCoverageApp.h"

// Add GTM_IS_COVERAGE_BUILD to your GCC_PREPROCESSOR_DEFINITIONS for the
// Xcode Configuration that wants CodeCoverage support.
#if GTM_IS_COVERAGE_BUILD

extern void __gcov_flush();

@implementation UIApplication(GTMCodeCoverage)

- (void)gtm_gcov_flush {
  __gcov_flush();
}

#if GTM_USING_XCTEST

+ (void)load {
  // Using defines and strings so that we don't have to link in
  // XCTest here.
  // Must set defaults here. If we set them in XCTest we are too late
  // for the observer registration.
  // See the documentation of XCTestObserverClassKey for why we set this key.
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  NSString *observers = [defaults stringForKey:GTMXCTestObserverClassKey];
  NSString *className = @"GTMCodeCoverageTests";
  if (observers == nil) {
    observers = GTMXCTestLogClass;
  }
  observers = [NSString stringWithFormat:@"%@,%@", observers, className];
  [defaults setValue:observers forKey:GTMXCTestObserverClassKey];
}

#endif  // GTM_USING_XCTEST

@end

#endif  // GTM_IS_COVERAGE_BUILD
