//
//  GTMStackTraceTest.m
//
//  Copyright 2007-2008 Google Inc.
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
#import "GTMStackTrace.h"
#import "GTMSenTestCase.h"

@interface GTMStackTraceTest : GTMTestCase
@end

@implementation GTMStackTraceTest
+ (BOOL)classMethodTest {
  NSString *stacktrace = GTMStackTrace();
  NSArray *stacklines = [stacktrace componentsSeparatedByString:@"\n"];
  NSString *firstFrame = [stacklines objectAtIndex:0];
  NSRange range = [firstFrame rangeOfString:@"+"];
  return range.location != NSNotFound;
}

- (void)testStackTraceBasic {
  NSString *stacktrace = GTMStackTrace();
  NSArray *stacklines = [stacktrace componentsSeparatedByString:@"\n"];

  XCTAssertGreaterThan([stacklines count], (NSUInteger)3,
                       @"stack trace must have > 3 lines");
  XCTAssertLessThan([stacklines count], (NSUInteger)100,
                    @"stack trace must have < 100 lines");

  NSString *firstFrame = [stacklines objectAtIndex:0];
  NSRange range = [firstFrame rangeOfString:@"testStackTraceBasic"];
  XCTAssertNotEqual(range.location, (NSUInteger)NSNotFound,
                    @"First frame should contain testStackTraceBasic,"
                    " stack trace: %@", stacktrace);
  range = [firstFrame rangeOfString:@"#0"];
  XCTAssertNotEqual(range.location, (NSUInteger)NSNotFound,
                    @"First frame should contain #0, stack trace: %@",
                    stacktrace);

  range = [firstFrame rangeOfString:@"-"];
  XCTAssertNotEqual(range.location, (NSUInteger)NSNotFound,
                    @"First frame should contain - since it's "
                    @"an instance method: %@", stacktrace);
  XCTAssertTrue([[self class] classMethodTest], @"First frame should contain"
                @"+ since it's a class method");
}

-(void)testGetStackAddressDescriptors {
  struct GTMAddressDescriptor descs[100];
  size_t depth = sizeof(descs) / sizeof(struct GTMAddressDescriptor);
  depth = GTMGetStackAddressDescriptors(descs, depth);
  // Got atleast 4...
  XCTAssertGreaterThan(depth, (size_t)4);
  // All that we got have symbols
  for (NSUInteger lp = 0 ; lp < depth ; ++lp) {
    XCTAssertNotNULL(descs[lp].symbol, @"didn't get a symbol at depth %lu",
                     (unsigned long)lp);
  }

  // Do it again, but don't give it enough space (to make sure it handles that)
  size_t fullDepth = depth;
  XCTAssertGreaterThan(fullDepth, (size_t)4);
  depth -= 2;
  depth = GTMGetStackAddressDescriptors(descs, depth);
  XCTAssertLessThan(depth, fullDepth);
  // All that we got have symbols
  for (NSUInteger lp = 0 ; lp < depth ; ++lp) {
    XCTAssertNotNULL(descs[lp].symbol, @"didn't get a symbol at depth %lu",
                     (unsigned long)lp);
  }

}

- (void)helperThatThrows {
  [NSException raise:@"TestException" format:@"TestExceptionDescription"];
}

- (void)testStackExceptionTrace {
  NSException *exception = nil;
  @try {
    [self helperThatThrows];
  }
  @catch (NSException * e) {
    exception = e;
  }
  XCTAssertNotNil(exception);
  NSString *stacktrace = GTMStackTraceFromException(exception);
  NSArray *stacklines = [stacktrace componentsSeparatedByString:@"\n"];

  XCTAssertGreaterThan([stacklines count], (NSUInteger)4,
                       @"stack trace must have > 4 lines\n%@", stacktrace);
  XCTAssertLessThan([stacklines count], (NSUInteger)100,
                    @"stack trace must have < 100 lines\n%@", stacktrace);
  XCTAssertEqual([stacklines count],
                 [[exception callStackReturnAddresses] count],
                 @"stack trace should have the same number of lines as the "
                 @" array of return addresses.  stack trace: %@", stacktrace);

  // we can't look for it on a specific frame because NSException doesn't
  // really document how deep the stack will be
  NSRange range = [stacktrace rangeOfString:@"testStackExceptionTrace"];
  XCTAssertNotEqual(range.location, (NSUInteger)NSNotFound,
                    @"Stack trace should contain testStackExceptionTrace,"
                    " stack trace: %@", stacktrace);
}

@end
