//
//  GTMSignalHandlerTest.m
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
#import "GTMSignalHandler.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMRegex
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMSignalHandlerTest : GTMTestCase
@end

@interface SignalCounter : NSObject {
  int signalCount_;
  int lastSeenSignal_;
}
- (int)count;
- (int)lastSeen;
- (void)countSignal:(int)signo;
+ (id)signalCounter;
@end // SignalCounter

@implementation SignalCounter
+ (id)signalCounter {
  return [[[self alloc] init] autorelease];
}

- (int)count {
  return signalCount_;
}

- (int)lastSeen {
  return lastSeenSignal_;
}

// Count the number of times this signal handler has fired.
- (void)countSignal:(int)signo {
  signalCount_++;
  lastSeenSignal_ = signo;
}

@end

@implementation GTMSignalHandlerTest
- (void)nomnomnom:(int)blah {
  XCTFail(@"Should never be called!");
}

- (void)testNillage {
  GTMSignalHandler *handler;

  // Just an init should return nil.
  handler = [[[GTMSignalHandler alloc] init] autorelease];
  XCTAssertNil(handler);

  // Zero signal should return nil as well.
  handler = [[[GTMSignalHandler alloc]
              initWithSignal:0
                      target:self
                      action:@selector(nomnomnom:)] autorelease];
  XCTAssertNil(handler);

}

- (void)testSingleHandler {
  // SIGIO and SIGWINCH were chosen for this test because LLDB does not trap
  // them which allows you to run this test under the debugger.
  // If you need to use other signals and the debugger is getting annoying
  // https://stackoverflow.com/questions/11984051/how-to-tell-lldb-debugger-not-to-handle-sigbus
  SignalCounter *counter = [SignalCounter signalCounter];

  // Raising our signals off of a background queue becuase raising them
  // off of dispatch_main_queue does not work with a CFRunLoop.
  // https://openradar.appspot.com/radar?id=5030997057863680
  dispatch_queue_t raiseQueue =
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  const int deltaT = 5000000;
  XCTAssertNotNil(counter);

  GTMSignalHandler *handler = [[[GTMSignalHandler alloc]
                                 initWithSignal:SIGWINCH
                                         target:counter
                                         action:@selector(countSignal:)]
                               autorelease];
  XCTAssertNotNil(handler);

  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.lastSeen == %d", SIGWINCH]
            evaluatedWithObject:counter handler:NULL];
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.count == 1"]
            evaluatedWithObject:counter handler:NULL];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, deltaT), raiseQueue, ^{
    // Using dispatch_after to make sure our signal is sent AFTER the runloop
    // is being spun in waitForExpectationsWithTimeout.
    raise(SIGWINCH);
  });
  [self waitForExpectationsWithTimeout:5 handler:NULL];
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.lastSeen == %d", SIGWINCH]
            evaluatedWithObject:counter handler:NULL];
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.count == 2"]
            evaluatedWithObject:counter handler:NULL];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, deltaT), raiseQueue, ^{
    raise(SIGWINCH);
  });
  [self waitForExpectationsWithTimeout:5 handler:NULL];

  // create a second one to make sure we're sending data where we want
  SignalCounter *counter2 = [SignalCounter signalCounter];
  XCTAssertNotNil(counter2);
  [[[GTMSignalHandler alloc] initWithSignal:SIGIO
      target:counter2
      action:@selector(countSignal:)] autorelease];
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.lastSeen == %d", SIGIO]
            evaluatedWithObject:counter2 handler:NULL];
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.count == 1"]
            evaluatedWithObject:counter2 handler:NULL];

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, deltaT), raiseQueue, ^{
    raise(SIGIO);
  });
  [self waitForExpectationsWithTimeout:5 handler:NULL];

  XCTAssertEqual([counter count], 2);
  XCTAssertEqual([counter lastSeen], SIGWINCH);

  [handler invalidate];

  // The signal is still ignored (so we shouldn't die), but the
  // the handler method should not get called.
  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.lastSeen == %d", SIGWINCH]
            evaluatedWithObject:counter handler:NULL].inverted = YES;
  [self expectationForPredicate:
      [NSPredicate predicateWithFormat:@"self.count == 2"]
            evaluatedWithObject:counter handler:NULL].inverted = YES;
  raise(SIGWINCH);
  [self waitForExpectationsWithTimeout:.2 handler:NULL];
}

- (void)testIgnore {
  SignalCounter *counter = [SignalCounter signalCounter];
  XCTAssertNotNil(counter);

  [[[GTMSignalHandler alloc] initWithSignal:SIGIO
                                     target:counter
                                     action:NULL] autorelease];

  [self expectationForPredicate:
       [NSPredicate predicateWithFormat:@"self.count == 0"]
            evaluatedWithObject:counter handler:NULL].inverted = YES;
  raise(SIGIO);
  [self waitForExpectationsWithTimeout:.2 handler:NULL];
}

@end

#pragma clang diagnostic pop
