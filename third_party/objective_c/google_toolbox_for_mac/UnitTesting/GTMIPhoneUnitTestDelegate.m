//
//  GTMIPhoneUnitTestDelegate.m
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

#import "GTMIPhoneUnitTestDelegate.h"

#import "GTMCodeCoverageApp.h"
#import "GTMDefines.h"
#if !GTM_IPHONE_SDK
#error GTMIPhoneUnitTestDelegate for iPhone only
#endif
#import <objc/runtime.h>
#import <stdio.h>
#import <UIKit/UIKit.h>
#import "GTMSenTestCase.h"

#pragma clang diagnostic push
#if !GTM_USING_XCTEST
// Turn off the deprecated warning when GTMTestCase is still based on SenTest.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif  // !GTM_USING_XCTEST

@interface UIApplication (GTMIPhoneUnitTestDelegate)

// SPI that we need to exit cleanly with a value.
- (void)_terminateWithStatus:(int)status;

@end

@interface GTMIPhoneUnitTestDelegate ()
// We have cases where we are created in UIApplicationMain, but then the
// user accidentally/intentionally replaces us as a delegate in their xib file
// which means that we never get the applicationDidFinishLaunching: message.
// We can register for the notification, but when the applications delegate
// is reset, it releases us, and we get dealloced. Therefore we have retainer
// which is responsible for retaining us until we get the notification.
// We do it through this slightly roundabout route (instead of just an extra
// retain in the init) so that clang doesn't complain about a leak.
// We also check to make sure we aren't called twice with the
// applicationDidFinishLaunchingCalled flag.
@property (readwrite, retain, nonatomic) GTMIPhoneUnitTestDelegate *retainer;

- (void)runTestsAndExit:(UIApplication *)application;

@end

@implementation GTMIPhoneUnitTestDelegate

@synthesize retainer = retainer_;

- (id)init {
  if ((self = [super init])) {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self
           selector:@selector(handleApplicationDidFinishLaunchingNotification:)
               name:UIApplicationDidFinishLaunchingNotification
             object:[UIApplication sharedApplication]];
    [self setRetainer:self];
  }
  return self;
}

// If running in the mode were we get the notification, bridge it over to
// the method we'd get if we are the app delegate.
- (void)handleApplicationDidFinishLaunchingNotification:(NSNotification *)note {
  [self applicationDidFinishLaunching:[note object]];
}

// Run through all the registered classes and run test methods on any
// that are subclasses of SenTestCase. Terminate the application upon
// test completion.
- (void)applicationDidFinishLaunching:(UIApplication *)application {

  // We could get called twice once from our notification registration, and
  // once if we actually still are the delegate of the application after
  // it has finished launching. So we'll just return if we've been called once.
  if (applicationDidFinishLaunchingCalled_) return;
  applicationDidFinishLaunchingCalled_ = YES;

  NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
  [nc removeObserver:self
                name:UIApplicationDidFinishLaunchingNotification
              object:application];

  // To permit UI-based tests, run the tests and exit app in a delayed selector
  // call. This ensures tests are run outside of applicationDidFinishLaunching:.
  [self performSelector:@selector(runTestsAndExit:)
             withObject:application
             afterDelay:0];
}

// Run through all tests and then exit application if required.
- (void)runTestsAndExit:(UIApplication *)application {
  [self runTests];

  if ([application respondsToSelector:@selector(gtm_gcov_flush)]) {
    [application performSelector:@selector(gtm_gcov_flush)];
  }

  if (!getenv("GTM_DISABLE_TERMINATION")) {
    // To help using xcodebuild, make the exit status 0/1 to signal the tests
    // success/failure.
    int exitStatus = (([self totalFailures] == 0U) ? 0 : 1);
    // Alternative to exit(status); so it cleanly terminates the UIApplication
    // and classes that depend on this signal to exit cleanly.
    NSMethodSignature * terminateSignature
      = [UIApplication instanceMethodSignatureForSelector:@selector(_terminateWithStatus:)];
    if (terminateSignature != nil) {
      NSInvocation * terminateInvocation
        = [NSInvocation invocationWithMethodSignature:terminateSignature];
      [terminateInvocation setTarget:application];
      [terminateInvocation setSelector:@selector(_terminateWithStatus:)];
      [terminateInvocation setArgument:&exitStatus atIndex:2];
      [terminateInvocation invoke];
    } else {
      exit(exitStatus);
    }
  }

  // Release ourself now that we're done. If we really are the application
  // delegate, it will have retained us, so we'll stick around if necessary.
  [self setRetainer:nil];
}

static int ClassSort(const void *a, const void *b) {
  Class *classA = (Class *)a;
  Class *classB = (Class *)b;
  const char *nameA = class_getName(*classA);
  const char *nameB = class_getName(*classB);
  return strcmp(nameA, nameB);
}

// Run through all the registered classes and run test methods on any
// that are subclasses of SenTestCase. Print results and run time to
// the default output.
- (void)runTests {
  int count = objc_getClassList(NULL, 0);
  NSMutableData *classData
    = [NSMutableData dataWithLength:sizeof(Class) * count];
  Class *classes = (Class*)[classData mutableBytes];
  _GTMDevAssert(classes, @"Couldn't allocate class list");
  objc_getClassList(classes, count);

  // Follow SenTest's lead and sort the classes.  (This may only be a change
  // in the iOS 5 runtime, but make sure it is always sorted just incase)
  mergesort(classes, count, sizeof(Class), ClassSort);

  totalFailures_ = 0;
  totalSuccesses_ = 0;
  NSString *suiteName = [[NSBundle mainBundle] bundlePath];
  NSDate *suiteStartDate = [NSDate date];
  NSString *suiteStartString
    = [NSString stringWithFormat:@"Test Suite '%@' started at %@\n",
                                 suiteName, suiteStartDate];
  fputs([suiteStartString UTF8String], stderr);
  fflush(stderr);
  for (int i = 0; i < count; ++i) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    Class currClass = classes[i];
    if (class_respondsToSelector(currClass, @selector(conformsToProtocol:)) &&
        [currClass conformsToProtocol:@protocol(SenTestCase)]) {
      NSArray *invocations = [currClass testInvocations];
      if ([invocations count]) {
        NSDate *fixtureStartDate = [NSDate date];
        NSString *fixtureName = NSStringFromClass(currClass);
        NSString *fixtureStartString
          = [NSString stringWithFormat:@"Test Suite '%@' started at %@\n",
                                       fixtureName, fixtureStartDate];
        int fixtureSuccesses = 0;
        int fixtureFailures = 0;
        fputs([fixtureStartString UTF8String], stderr);
        fflush(stderr);
        NSInvocation *invocation;
        for (invocation in invocations) {
          GTMTestCase *testCase
            = [[currClass alloc] initWithInvocation:invocation];
          BOOL failed = NO;
          NSDate *caseStartDate = [NSDate date];
          NSString *selectorName = NSStringFromSelector([invocation selector]);
          NSString *caseStartString
            = [NSString stringWithFormat:@"Test Case '-[%@ %@]' started.\n",
               fixtureName, selectorName];
          fputs([caseStartString UTF8String], stderr);
          fflush(stderr);
          @try {
            [testCase performTest];
          } @catch (NSException *exception) {
            failed = YES;
          }
          if (failed) {
            fixtureFailures += 1;
          } else {
            fixtureSuccesses += 1;
          }
          NSTimeInterval caseEndTime
            = [[NSDate date] timeIntervalSinceDate:caseStartDate];
          NSString *caseEndString
            = [NSString stringWithFormat:@"Test Case '-[%@ %@]' %@ (%0.3f "
               "seconds).\n",
               fixtureName, selectorName,
               failed ? @"failed" : @"passed",
               caseEndTime];
          fputs([caseEndString UTF8String], stderr);
          fflush(stderr);
          [testCase release];
        }
        NSDate *fixtureEndDate = [NSDate date];
        NSTimeInterval fixtureEndTime
          = [fixtureEndDate timeIntervalSinceDate:fixtureStartDate];
        NSString *fixtureEndString
          = [NSString stringWithFormat:@"Test Suite '%@' finished at %@.\n"
             "Executed %d tests, with %d failures (%d "
             "unexpected) in %0.3f (%0.3f) seconds\n\n",
             fixtureName, fixtureEndDate,
             fixtureSuccesses + fixtureFailures,
             fixtureFailures, fixtureFailures,
             fixtureEndTime, fixtureEndTime];
        fputs([fixtureEndString UTF8String], stderr);
        fflush(stderr);
        totalSuccesses_ += fixtureSuccesses;
        totalFailures_ += fixtureFailures;
      }
    }
    [pool release];
  }
  NSDate *suiteEndDate = [NSDate date];
  NSTimeInterval suiteEndTime
    = [suiteEndDate timeIntervalSinceDate:suiteStartDate];
  NSString *suiteEndString
    = [NSString stringWithFormat:@"Test Suite '%@' finished at %@.\n"
                                 "Executed %tu tests, with %tu failures (%tu "
                                 "unexpected) in %0.3f (%0.3f) seconds\n\n",
                                 suiteName, suiteEndDate,
                                 totalSuccesses_ + totalFailures_,
                                 totalFailures_, totalFailures_,
                                 suiteEndTime, suiteEndTime];
  fputs([suiteEndString UTF8String], stderr);
  fflush(stderr);
}

- (NSUInteger)totalSuccesses {
  return totalSuccesses_;
}

- (NSUInteger)totalFailures {
  return totalFailures_;
}

@end

#pragma clang diagnostic pop
