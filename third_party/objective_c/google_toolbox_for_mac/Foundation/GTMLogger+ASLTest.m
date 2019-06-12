//
//  GTMLogger+ASLTest.m
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

#import "GTMLogger+ASL.h"
#import "GTMSenTestCase.h"

@interface DummyASLClient : GTMLoggerASLClient {
 @private
  NSString *facility_;
}
@end

static NSMutableArray *gDummyLog;  // weak

@implementation DummyASLClient

- (id)initWithFacility:(NSString *)facility {
  if ((self = [super initWithFacility:facility])) {
    facility_ = [facility copy];
  }
  return self;
}

- (void)dealloc {
  [facility_ release];
  [super dealloc];
}

- (void)log:(NSString *)msg level:(int)level {
  NSString *line = [NSString stringWithFormat:@"%@-%@-%d",
                       (facility_ ? facility_ : @""), msg, level];
  [gDummyLog addObject:line];
}

@end


@interface GTMLogger_ASLTest : GTMTestCase
@end

@implementation GTMLogger_ASLTest

- (void)testCreation {
  GTMLogger *aslLogger = [GTMLogger standardLoggerWithASL];
  XCTAssertNotNil(aslLogger);

  GTMLogASLWriter *writer = [GTMLogASLWriter aslWriter];
  XCTAssertNotNil(writer);
}

- (void)testLogWriter {
  gDummyLog = [[[NSMutableArray alloc] init] autorelease];
  GTMLogASLWriter *writer = [[[GTMLogASLWriter alloc]
                              initWithClientClass:[DummyASLClient class]
                                         facility:nil]
                             autorelease];


  XCTAssertNotNil(writer);
  XCTAssertEqual([gDummyLog count], (NSUInteger)0);

  // Log some messages
  [writer logMessage:@"unknown" level:kGTMLoggerLevelUnknown];
  [writer logMessage:@"debug" level:kGTMLoggerLevelDebug];
  [writer logMessage:@"info" level:kGTMLoggerLevelInfo];
  [writer logMessage:@"error" level:kGTMLoggerLevelError];
  [writer logMessage:@"assert" level:kGTMLoggerLevelAssert];

  // Inspect the logged message to make sure they were logged correctly. The
  // dummy writer will save the messages w/ @level concatenated. The "level"
  // will be the ASL level, not the GTMLogger level. GTMLogASLWriter will log
  // all
  NSArray *expected = [NSArray arrayWithObjects:
                       @"-unknown-5",
                       @"-debug-5",
                       @"-info-5",
                       @"-error-3",
                       @"-assert-1",
                       nil];

  XCTAssertEqualObjects(gDummyLog, expected);
  [gDummyLog removeAllObjects];

  // Same test with facility
  writer = [[[GTMLogASLWriter alloc]
               initWithClientClass:[DummyASLClient class]
                          facility:@"testfac"] autorelease];


  XCTAssertNotNil(writer);
  XCTAssertEqual([gDummyLog count], (NSUInteger)0);

  [writer logMessage:@"unknown" level:kGTMLoggerLevelUnknown];
  [writer logMessage:@"debug" level:kGTMLoggerLevelDebug];
  [writer logMessage:@"info" level:kGTMLoggerLevelInfo];
  [writer logMessage:@"error" level:kGTMLoggerLevelError];
  [writer logMessage:@"assert" level:kGTMLoggerLevelAssert];
  expected = [NSArray arrayWithObjects:
                @"testfac-unknown-5",
                @"testfac-debug-5",
                @"testfac-info-5",
                @"testfac-error-3",
                @"testfac-assert-1",
                nil];
  XCTAssertEqualObjects(gDummyLog, expected);

  gDummyLog = nil;
}

- (void)testASLClient {
  GTMLoggerASLClient *client = [[GTMLoggerASLClient alloc] init];
  XCTAssertNotNil(client);
  [client release];
}

@end
