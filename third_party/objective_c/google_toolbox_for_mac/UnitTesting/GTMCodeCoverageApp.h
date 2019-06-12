//
//  GTMCodeCovereageApp.h
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

#import <UIKit/UIKit.h>


// Add GTM_IS_COVERAGE_BUILD to your GCC_PREPROCESSOR_DEFINITIONS for the
// Xcode Configuration that wants CodeCoverage support.
#ifndef GTM_IS_COVERAGE_BUILD
#define GTM_IS_COVERAGE_BUILD 0
#endif

// If you are using this with XCTest (as opposed to SenTestingKit)
// make sure to define GTM_USING_XCTEST.
#ifndef GTM_USING_XCTEST
#define GTM_USING_XCTEST 0
#endif

// NOTE: As of Xcode 6, Apple made XCTestObserver and XCTestLog deprecated without
// having a replacement. Things still seem to work, but there doesn't seem to be a
// different way yet to hook when the tests finish.
// radr/18395261 - XCTestObserver deprecated with no replacement

#define GTMXCTestObserverClassKey @"XCTestObserverClass"
#define GTMXCTestLogClass @"XCTestLog"

@interface UIApplication(GTMCodeCoverage)
- (void)gtm_gcov_flush;
@end

