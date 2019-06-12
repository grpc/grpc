//
//  GTMFoundationUnitTestingUtilities.h
//
//  Copyright 2006-2010 Google Inc.
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
#import <objc/objc.h>

// NOTE:  These utilities predate XCTestExpectation (introduced with Xcode 6).
//        Newer unit tests should use [self waitForExpectationsWithTimeout:]
//        to spin the run loop instead of using the context utilities below.

// Many tests need to spin the runloop and wait for an event to happen. This is
// often done by calling:
// NSDate* next = [NSDate dateWithTimeIntervalSinceNow:resolution];
// [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
//                          beforeDate:next];
// where |resolution| is a guess at how long it will take for the event to
// happen. There are two major problems with this approach:
// a) By guessing we force the test to take at least |resolution| time.
// b) It makes for flaky tests in that sometimes this guess isn't good, and the
//    test takes slightly longer than |resolution| time causing the test to post
//    a possibly false-negative failure.
// To make timing callback tests easier use this class and the
// GTMUnitTestingAdditions additions to NSRunLoop and NSApplication.
// Your call would look something like this:
// id<GTMUnitTestingRunLoopContext> context = [self getMeAContext];
// [[NSRunLoop currentRunLoop] gtm_runUpToSixtySecondsWithContext:context];
// Then in some callback method within your test you would call
// [context setShouldStop:YES];
// Internally gtm_runUpToSixtySecondsWithContext will poll the runloop really
// quickly to keep test times down to a minimum, but will stop after a good time
// interval (in this case 60 seconds) for failures.
@protocol GTMUnitTestingRunLoopContext
// Return YES if the NSRunLoop (or equivalent) that this context applies to
// should stop as soon as possible.
- (BOOL)shouldStop;
@end

// Collection of utilities for unit testing
@interface GTMFoundationUnitTestingUtilities : NSObject

// Returns YES if we are currently being unittested.
+ (BOOL)areWeBeingUnitTested;


// Installs a timer to quit the process after the given time, as a catch all for
// something not working.  There is a problem that of the testing bundle fails
// to load when is is being hosted in a custom app, the app will remain running
// until the user quits it.  This provides a way out of that.  When the timer
// fires, a message is logged, and the process is directly exited, no clean
// shutdown.  This requires a runloop be running.
+ (void)installTestingTimeout:(NSTimeInterval)maxRunInterval;

@end

// An implementation of the GTMUnitTestingRunLoopContext that is a simple
// BOOL flag. See GTMUnitTestingRunLoopContext documentation.
@interface GTMUnitTestingBooleanRunLoopContext : NSObject <GTMUnitTestingRunLoopContext> {
 @private
  BOOL shouldStop_;
}
+ (id)context;
- (BOOL)shouldStop;
- (void)setShouldStop:(BOOL)stop;
- (void)reset;
@end

// Some category methods to simplify spinning the runloops in such a way as
// to make tests less flaky, but have them complete as fast as possible.
@interface NSRunLoop (GTMUnitTestingAdditions)
// Will spin the runloop in mode until date in mode until the runloop returns
// because all sources have been removed or the current date is greater than
// |date| or [context shouldStop] returns YES.
// Return YES if the runloop was stopped because [context shouldStop] returned
// YES.
- (BOOL)gtm_runUntilDate:(NSDate *)date
                    mode:(NSString *)mode
                 context:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");

// Calls -gtm_runUntilDate:mode:context: with mode set to NSDefaultRunLoopMode.
- (BOOL)gtm_runUntilDate:(NSDate *)date
                 context:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");

// Calls -gtm_runUntilDate:mode:context: with mode set to NSDefaultRunLoopMode,
// and the timeout date set to |seconds| seconds.
- (BOOL)gtm_runUpToNSeconds:(NSTimeInterval)seconds
                    context:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");

// Calls -gtm_runUntilDate:mode:context: with mode set to NSDefaultRunLoopMode,
// and the timeout date set to 60 seconds.
// This is a good time to use for AppleEvent calls (which default to 60 seconds)
// but may be a bit long for standard unit tests, and could cause a long unit
// testing run if you have multiple failures.
// Calling -[gtm_runUpToNSeconds:context:] is preferred.
- (BOOL)gtm_runUpToSixtySecondsWithContext:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");

@end
