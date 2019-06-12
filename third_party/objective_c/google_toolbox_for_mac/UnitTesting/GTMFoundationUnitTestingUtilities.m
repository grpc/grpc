//
//  GTMFoundationUnitTestingUtilities.m
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

#import <unistd.h>
#import "GTMDefines.h"
#import "GTMFoundationUnitTestingUtilities.h"

@implementation GTMFoundationUnitTestingUtilities

// Returns YES if we are currently being unittested.
+ (BOOL)areWeBeingUnitTested {
  BOOL answer = NO;
  Class testProbeClass;
#if GTM_USING_XCTEST
  testProbeClass = NSClassFromString(@"XCTestProbe");
#else
  testProbeClass = NSClassFromString(@"SenTestProbe");
#endif
  if (testProbeClass != Nil) {
    // Doing this little dance so we don't actually have to link
    // SenTestingKit in
    SEL selector = NSSelectorFromString(@"isTesting");
    NSMethodSignature *sig = [testProbeClass methodSignatureForSelector:selector];
    NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:sig];
    [invocation setSelector:selector];
    [invocation invokeWithTarget:testProbeClass];
    [invocation getReturnValue:&answer];
  }
  return answer;
}

+ (void)maxRuntimeTimer:(NSTimer*)timer {
  // Directly use fprintf so the message always shows up.
  fprintf(stderr, "%s:%d: error: %s : Hit app testing timeout!\n",
          __FILE__, __LINE__, __func__);
  fflush(stderr);
  exit(1);
}

+ (void)installTestingTimeout:(NSTimeInterval)maxRunInterval {
  [NSTimer scheduledTimerWithTimeInterval:maxRunInterval
                                   target:self
                                 selector:@selector(maxRuntimeTimer:)
                                 userInfo:nil
                                  repeats:NO];
}

@end

@implementation GTMUnitTestingBooleanRunLoopContext

+ (id)context {
  return [[[GTMUnitTestingBooleanRunLoopContext alloc] init] autorelease];
}

- (BOOL)shouldStop {
  return shouldStop_;
}

- (void)setShouldStop:(BOOL)stop {
  shouldStop_ = stop;
}

- (void)reset {
  shouldStop_ = NO;
}

@end

@implementation NSRunLoop (GTMUnitTestingAdditions)

- (BOOL)gtm_runUpToNSeconds:(NSTimeInterval)seconds
                    context:(id<GTMUnitTestingRunLoopContext>)context {
  return [self gtm_runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]
                        context:context];
}

- (BOOL)gtm_runUpToSixtySecondsWithContext:(id<GTMUnitTestingRunLoopContext>)context {
  return [self gtm_runUntilDate:[NSDate dateWithTimeIntervalSinceNow:60]
                        context:context];
}

- (BOOL)gtm_runUntilDate:(NSDate *)date
                 context:(id<GTMUnitTestingRunLoopContext>)context {
  return [self gtm_runUntilDate:date mode:NSDefaultRunLoopMode context:context];
}

- (BOOL)gtm_runUntilDate:(NSDate *)date
                    mode:(NSString *)mode
                 context:(id<GTMUnitTestingRunLoopContext>)context {
  BOOL contextShouldStop = NO;
  NSDate* next = nil;
  while (1) {
    contextShouldStop = [context shouldStop];
    if (contextShouldStop) break;
    next = [[NSDate alloc] initWithTimeIntervalSinceNow:0.01];
    if (!([self runMode:mode beforeDate:next])) break;
    if ([next compare:date] == NSOrderedDescending) break;
    [next release];
    next = nil;
  }
  [next release];
  return contextShouldStop;
}

@end
