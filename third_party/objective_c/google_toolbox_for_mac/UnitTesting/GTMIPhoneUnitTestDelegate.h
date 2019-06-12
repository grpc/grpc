//
//  GTMIPhoneUnitTestDelegate.h
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

#import <Foundation/Foundation.h>

// Application delegate that runs all test methods in registered classes
// extending SenTestCase. The application is terminated afterwards.
// You can also run the tests directly from your application by invoking
// runTests and clean up, restore data, etc. before the application
// terminates.
@interface GTMIPhoneUnitTestDelegate : NSObject {
 @private
  NSUInteger totalFailures_;
  NSUInteger totalSuccesses_;
  BOOL applicationDidFinishLaunchingCalled_;
  GTMIPhoneUnitTestDelegate *retainer_;
}

// Runs through all the registered classes and runs test methods on any
// that are subclasses of SenTestCase. Prints results and run time to
// the default output.
- (void)runTests;
// Fetch the number of successes or failures from the last runTests.
- (NSUInteger)totalSuccesses;
- (NSUInteger)totalFailures;
@end
