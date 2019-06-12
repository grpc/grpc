//
//  GTMLoggerTest.m
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

#import "GTMLogger.h"
#import "GTMRegex.h"
#import "GTMSenTestCase.h"


// A test writer that stores log messages in an array for easy retrieval.
@interface ArrayWriter : NSObject <GTMLogWriter> {
 @private
  NSMutableArray *messages_;
}
- (NSArray *)messages;
- (void)clear;
@end
@implementation ArrayWriter
- (id)init {
  if ((self = [super init])) {
    messages_ = [[NSMutableArray alloc] init];
  }
  return self;
}
- (void)dealloc {
  [messages_ release];
  [super dealloc];
}
- (NSArray *)messages {
  return messages_;
}
- (void)logMessage:(NSString *)msg level:(GTMLoggerLevel)level {
  [messages_ addObject:msg];
}
- (void)clear {
  [messages_ removeAllObjects];
}
@end  // ArrayWriter


// A formatter for testing that prepends the word DUMB to log messages, along
// with the log level number.
@interface DumbFormatter : GTMLogBasicFormatter
@end
@implementation DumbFormatter

#if !defined(__clang__) && (__GNUC__*10+__GNUC_MINOR__ >= 42)
// Some versions of GCC (4.2 and below AFAIK) aren't great about supporting
// -Wmissing-format-attribute
// when the function is anything more complex than foo(NSString *fmt, ...).
// You see the error inside the function when you turn ... into va_args and
// attempt to call another function (like vsprintf for example).
// So we just shut off the warning for this function.
#pragma GCC diagnostic ignored "-Wmissing-format-attribute"
#endif  // !__clang__

- (NSString *)stringForFunc:(NSString *)func
                 withFormat:(NSString *)fmt
                     valist:(va_list)args
                      level:(GTMLoggerLevel)level {
  return [NSString stringWithFormat:@"DUMB [%d] %@", level,
          [super stringForFunc:nil withFormat:fmt valist:args level:level]];
}

#if !defined(__clang__) && (__GNUC__*10+__GNUC_MINOR__ >= 42)
#pragma GCC diagnostic error "-Wmissing-format-attribute"
#endif  // !__clang__

@end  // DumbFormatter


// A test filter that ignores messages with the string "ignore".
@interface IgnoreFilter : NSObject <GTMLogFilter>
@end
@implementation IgnoreFilter
- (BOOL)filterAllowsMessage:(NSString *)msg level:(GTMLoggerLevel)level {
  NSRange range = [msg rangeOfString:@"ignore"];
  return (range.location == NSNotFound);
}
@end  // IgnoreFilter

//
// Begin test harness
//

@interface GTMLoggerTest : GTMTestCase {
 @private
  NSString *path_;
}

- (NSString *)stringFromFormatter:(id<GTMLogFormatter>)formatter
                            level:(GTMLoggerLevel)level
                           format:(NSString *)fmt, ... NS_FORMAT_FUNCTION(3,4);
@end

@implementation GTMLoggerTest

- (void)setUp {
  path_ = [[NSTemporaryDirectory() stringByAppendingPathComponent:
            @"GTMLoggerUnitTest.log"] retain];
  XCTAssertNotNil(path_);
  // Make sure we're cleaned up from the last run
  [[NSFileManager defaultManager] removeItemAtPath:path_ error:NULL];
}

- (void)tearDown {
  XCTAssertNotNil(path_);
  [[NSFileManager defaultManager] removeItemAtPath:path_ error:NULL];
  [path_ release];
  path_ = nil;
}

- (void)testCreation {
  GTMLogger *logger1 = nil, *logger2 = nil;

  logger1 = [GTMLogger sharedLogger];
  logger2 = [GTMLogger sharedLogger];

  XCTAssertTrue(logger1 == logger2);

  XCTAssertNotNil([logger1 writer]);
  XCTAssertNotNil([logger1 formatter]);
  XCTAssertNotNil([logger1 filter]);

  // Get a new instance; not the shared instance
  logger2 = [GTMLogger standardLogger];

  XCTAssertTrue(logger1 != logger2);
  XCTAssertNotNil([logger2 writer]);
  XCTAssertNotNil([logger2 formatter]);
  XCTAssertNotNil([logger2 filter]);

  // Set the new instance to be the shared logger.
  [GTMLogger setSharedLogger:logger2];
  XCTAssertTrue(logger2 == [GTMLogger sharedLogger]);
  XCTAssertTrue(logger1 != [GTMLogger sharedLogger]);

  // Set the shared logger to nil, which should reset it to a new "standard"
  // logger.
  [GTMLogger setSharedLogger:nil];
  XCTAssertNotNil([GTMLogger sharedLogger]);
  XCTAssertTrue(logger2 != [GTMLogger sharedLogger]);
  XCTAssertTrue(logger1 != [GTMLogger sharedLogger]);

  GTMLogger *logger = [GTMLogger logger];
  XCTAssertNotNil(logger);

  logger = [GTMLogger standardLoggerWithStderr];
  XCTAssertNotNil(logger);

  logger = [GTMLogger standardLoggerWithPath:path_];
  XCTAssertNotNil(logger);
}

- (void)testAccessors {
  GTMLogger *logger = [GTMLogger standardLogger];
  XCTAssertNotNil(logger);

  XCTAssertNotNil([logger writer]);
  XCTAssertNotNil([logger formatter]);
  XCTAssertNotNil([logger filter]);

  [logger setWriter:nil];
  [logger setFormatter:nil];
  [logger setFilter:nil];

  // These attributes should NOT be nil. They should be set to their defaults.
  XCTAssertNotNil([logger writer]);
  XCTAssertNotNil([logger formatter]);
  XCTAssertNotNil([logger filter]);
}

- (void)testLogger {
  ArrayWriter *writer = [[[ArrayWriter alloc] init] autorelease];
  IgnoreFilter *filter = [[[IgnoreFilter alloc] init] autorelease];

  // We actually only need the array writer instance for this unit test to pass,
  // but we combine that writer with a stdout writer for two reasons:
  //
  //   1. To test the NSArray composite writer object
  //   2. To make debugging easier by sending output to stdout
  //
  // We also include in the array an object that is not a GTMLogWriter to make
  // sure that we don't crash when presented with an array of non-GTMLogWriters.
  NSArray *writers = [NSArray arrayWithObjects:writer,
                      [NSFileHandle fileHandleWithStandardOutput],
                      @"blah", nil];

  GTMLogger *logger = [GTMLogger loggerWithWriter:writers
                                        formatter:nil  // basic formatter
                                           filter:filter];

  XCTAssertNotNil(logger);

  // Log a few messages to test with
  [logger logInfo:@"hi"];
  [logger logDebug:@"foo"];
  [logger logError:@"blah"];
  [logger logAssert:@"baz"];

  // Makes sure the messages got logged
  NSArray *messages = [writer messages];
  XCTAssertNotNil(messages);
  XCTAssertEqual([messages count], (NSUInteger)4);
  XCTAssertEqualObjects([messages objectAtIndex:0], @"hi");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"foo");
  XCTAssertEqualObjects([messages objectAtIndex:2], @"blah");
  XCTAssertEqualObjects([messages objectAtIndex:3], @"baz");

  // Log a message that should be ignored, and make sure it did NOT get logged
  [logger logInfo:@"please ignore this"];
  messages = [writer messages];
  XCTAssertNotNil(messages);
  XCTAssertEqual([messages count], (NSUInteger)4);
  XCTAssertEqualObjects([messages objectAtIndex:0], @"hi");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"foo");
  XCTAssertEqualObjects([messages objectAtIndex:2], @"blah");
  XCTAssertEqualObjects([messages objectAtIndex:3], @"baz");

  // Change the formatter to our "dumb formatter"
  id<GTMLogFormatter> formatter = [[[DumbFormatter alloc] init] autorelease];
  [logger setFormatter:formatter];

  [logger logInfo:@"bleh"];
  messages = [writer messages];
  XCTAssertNotNil(messages);
  XCTAssertEqual([messages count], (NSUInteger)5);  // Message count should increase
  // The previously logged messages should not change
  XCTAssertEqualObjects([messages objectAtIndex:0], @"hi");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"foo");
  XCTAssertEqualObjects([messages objectAtIndex:2], @"blah");
  XCTAssertEqualObjects([messages objectAtIndex:3], @"baz");
  XCTAssertEqualObjects([messages objectAtIndex:4], @"DUMB [2] bleh");
}

- (void)testConvenienceMacros {
  ArrayWriter *writer = [[[ArrayWriter alloc] init] autorelease];
  NSArray *writers = [NSArray arrayWithObjects:writer,
                      [NSFileHandle fileHandleWithStandardOutput], nil];

  [[GTMLogger sharedLogger] setWriter:writers];

  // Here we log a message using a convenience macro, which should log the
  // message along with the name of the function it was called from. Here we
  // test to make sure the logged message does indeed contain the name of the
  // current function "testConvenienceMacros".
  GTMLoggerError(@"test ========================");
  XCTAssertEqual([[writer messages] count], (NSUInteger)1);
  NSRange rangeOfFuncName =
    [[[writer messages] objectAtIndex:0] rangeOfString:@"testConvenienceMacros"];
  XCTAssertTrue(rangeOfFuncName.location != NSNotFound);
  [writer clear];

  [[GTMLogger sharedLogger] setFormatter:nil];

  GTMLoggerInfo(@"test %d", 1);
  GTMLoggerDebug(@"test %d", 2);
  GTMLoggerError(@"test %d", 3);
  GTMLoggerAssert(@"test %d", 4);

  NSArray *messages = [writer messages];
  XCTAssertNotNil(messages);

#ifdef DEBUG
  XCTAssertEqual([messages count], (NSUInteger)4);
  XCTAssertEqualObjects([messages objectAtIndex:0], @"test 1");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"test 2");
  XCTAssertEqualObjects([messages objectAtIndex:2], @"test 3");
  XCTAssertEqualObjects([messages objectAtIndex:3], @"test 4");
#else
  // In Release builds, only the Error and Assert messages will be logged
  XCTAssertEqual([messages count], (NSUInteger)2);
  XCTAssertEqualObjects([messages objectAtIndex:0], @"test 3");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"test 4");
#endif

}

- (void)testFileHandleWriter {
  NSFileHandle *fh = nil;

  fh = [NSFileHandle fileHandleForWritingAtPath:path_];
  XCTAssertNil(fh);

  fh = [NSFileHandle fileHandleForLoggingAtPath:path_ mode:0644];
  XCTAssertNotNil(fh);

  [fh logMessage:@"test 0" level:kGTMLoggerLevelUnknown];
  [fh logMessage:@"test 1" level:kGTMLoggerLevelDebug];
  [fh logMessage:@"test 2" level:kGTMLoggerLevelInfo];
  [fh logMessage:@"test 3" level:kGTMLoggerLevelError];
  [fh logMessage:@"test 4" level:kGTMLoggerLevelAssert];
  [fh closeFile];

  NSError *err = nil;
  NSString *contents = [NSString stringWithContentsOfFile:path_
                                                 encoding:NSUTF8StringEncoding
                                                    error:&err];
  XCTAssertNotNil(contents, @"Error loading log file: %@", err);
  XCTAssertEqualObjects(@"test 0\ntest 1\ntest 2\ntest 3\ntest 4\n", contents);
}

- (void)testLoggerAdapterWriter {
  ArrayWriter *writer = [[[ArrayWriter alloc] init] autorelease];
  XCTAssertNotNil(writer);

  GTMLogger *sublogger = [GTMLogger loggerWithWriter:writer
                                         formatter:nil
                                            filter:nil];
  XCTAssertNotNil(sublogger);

  GTMLogger *logger = [GTMLogger loggerWithWriter:sublogger
                                      formatter:nil
                                         filter:nil];

  XCTAssertNotNil(logger);

  // Log a few messages to test with
  [logger logInfo:@"hi"];
  [logger logDebug:@"foo"];
  [logger logError:@"blah"];
  [logger logAssert:@"assert"];

  // Makes sure the messages got logged
  NSArray *messages = [writer messages];
  XCTAssertNotNil(messages);
  XCTAssertEqual([messages count], (NSUInteger)4);
  XCTAssertEqualObjects([messages objectAtIndex:0], @"hi");
  XCTAssertEqualObjects([messages objectAtIndex:1], @"foo");
  XCTAssertEqualObjects([messages objectAtIndex:2], @"blah");
  XCTAssertEqualObjects([messages objectAtIndex:3], @"assert");
}

// Helper method to help testing GTMLogFormatters
- (NSString *)stringFromFormatter:(id<GTMLogFormatter>)formatter
                            level:(GTMLoggerLevel)level
                           format:(NSString *)fmt, ... {
  va_list args;
  va_start(args, fmt);
  NSString *msg = [formatter stringForFunc:nil
                                withFormat:fmt
                                    valist:args
                                     level:level];
  va_end(args);
  return msg;
}

- (void)testFunctionPrettifier {
  GTMLogBasicFormatter *fmtr = [[[GTMLogBasicFormatter alloc] init]
                                 autorelease];
  XCTAssertNotNil(fmtr);

  // Nil, empty and whitespace
  XCTAssertEqualObjects([fmtr prettyNameForFunc:nil], @"(unknown)");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@""], @"(unknown)");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"   \n\t"], @"(unknown)");

  // C99 __func__
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"main"], @"main()");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"main"], @"main()");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@" main "], @"main()");

  // GCC Obj-C __func__ and __PRETTY_FUNCTION__
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"+[Foo bar]"], @"+[Foo bar]");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@" +[Foo bar] "], @"+[Foo bar]");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"-[Foo baz]"], @"-[Foo baz]");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@" -[Foo baz] "], @"-[Foo baz]");

  // GCC C++ __PRETTY_FUNCTION__
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@"void a::sub(int)"],
                        @"void a::sub(int)");
  XCTAssertEqualObjects([fmtr prettyNameForFunc:@" void a::sub(int) "],
                        @"void a::sub(int)");
}

- (void)testBasicFormatter {
  id<GTMLogFormatter> fmtr = [[[GTMLogBasicFormatter alloc] init] autorelease];
  XCTAssertNotNil(fmtr);
  NSString *msg = nil;

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@"test"];
  XCTAssertEqualObjects(msg, @"test");

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@"test %d", 1];
  XCTAssertEqualObjects(msg, @"test 1");

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@"test %@", @"foo"];
  XCTAssertEqualObjects(msg, @"test foo");

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@""];
  XCTAssertEqualObjects(msg, @"");

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@"     "];
  XCTAssertEqualObjects(msg, @"     ");
}

- (void)testStandardFormatter {
  id<GTMLogFormatter> fmtr = [[[GTMLogStandardFormatter alloc] init] autorelease];
  XCTAssertNotNil(fmtr);

  // E.g. 2008-01-04 09:16:26.906 xctest[5567/0xa07d0f60] [lvl=1] (no func) test
  #define kFormatBasePattern @"[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} %@\\[[0-9]+/0x[0-9a-f]+\\] \\[lvl=[0-3]\\] \\(unknown\\) %@"

  NSString *msg = nil;
  NSString *executableName = [[[NSBundle mainBundle] executablePath] lastPathComponent];

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelDebug
                           format:@"test"];
  NSString *pattern = [NSString stringWithFormat:kFormatBasePattern, executableName, @"test"];
  XCTAssertTrue([msg gtm_matchesPattern:pattern], @"msg: %@", msg);

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelError
                           format:@"test %d", 1];
  pattern = [NSString stringWithFormat:kFormatBasePattern, executableName, @"test 1"];
  XCTAssertTrue([msg gtm_matchesPattern:pattern], @"msg: %@", msg);

  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelInfo
                           format:@"test %@", @"hi"];
  pattern = [NSString stringWithFormat:kFormatBasePattern, executableName, @"test hi"];
  XCTAssertTrue([msg gtm_matchesPattern:pattern], @"msg: %@", msg);


  msg = [self stringFromFormatter:fmtr
                            level:kGTMLoggerLevelUnknown
                           format:@"test"];
  pattern = [NSString stringWithFormat:kFormatBasePattern, executableName, @"test"];
  XCTAssertTrue([msg gtm_matchesPattern:pattern], @"msg: %@", msg);
}

- (void)testNoFilter {
  id<GTMLogFilter> filter = [[[GTMLogNoFilter alloc] init] autorelease];
  XCTAssertNotNil(filter);

  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelUnknown]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelDebug]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelInfo]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelError]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelAssert]);
  XCTAssertTrue([filter filterAllowsMessage:@"" level:kGTMLoggerLevelDebug]);
  XCTAssertTrue([filter filterAllowsMessage:nil level:kGTMLoggerLevelDebug]);
}

- (void)testMinimumFilter {
  id<GTMLogFilter> filter = [[[GTMLogMininumLevelFilter alloc]
                                initWithMinimumLevel:kGTMLoggerLevelInfo]
                                    autorelease];
  XCTAssertNotNil(filter);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelUnknown]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelDebug]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelInfo]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelError]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelAssert]);

  filter = [[[GTMLogMininumLevelFilter alloc]
               initWithMinimumLevel:kGTMLoggerLevelDebug] autorelease];
  XCTAssertNotNil(filter);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelUnknown]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelDebug]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelInfo]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelError]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelAssert]);

  // Cannot exceed min/max levels filter
  filter = [[[GTMLogMininumLevelFilter alloc]
               initWithMinimumLevel:kGTMLoggerLevelAssert + 1] autorelease];
  XCTAssertNil(filter);
  filter = [[[GTMLogMininumLevelFilter alloc]
               initWithMinimumLevel:kGTMLoggerLevelUnknown - 1] autorelease];
  XCTAssertNil(filter);
}

- (void)testMaximumFilter {
  id<GTMLogFilter> filter = [[[GTMLogMaximumLevelFilter alloc]
                                initWithMaximumLevel:kGTMLoggerLevelInfo]
                                    autorelease];
  XCTAssertNotNil(filter);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelUnknown]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelDebug]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelInfo]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelError]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelAssert]);

  filter = [[[GTMLogMaximumLevelFilter alloc]
               initWithMaximumLevel:kGTMLoggerLevelDebug] autorelease];
  XCTAssertNotNil(filter);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelUnknown]);
  XCTAssertTrue([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelDebug]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelInfo]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelError]);
  XCTAssertFalse([filter filterAllowsMessage:@"hi" level:kGTMLoggerLevelAssert]);

  // Cannot exceed min/max levels filter
  filter = [[[GTMLogMaximumLevelFilter alloc]
               initWithMaximumLevel:kGTMLoggerLevelAssert + 1] autorelease];
  XCTAssertNil(filter);
  filter = [[[GTMLogMaximumLevelFilter alloc]
               initWithMaximumLevel:kGTMLoggerLevelUnknown - 1] autorelease];
  XCTAssertNil(filter);
}

- (void)testFileHandleCreation {
  NSFileHandle *fh = nil;

  fh = [NSFileHandle fileHandleForLoggingAtPath:nil mode:0644];
  XCTAssertNil(fh);

  fh = [NSFileHandle fileHandleForLoggingAtPath:path_ mode:0644];
  XCTAssertNotNil(fh);

  [fh logMessage:@"test 1" level:kGTMLoggerLevelInfo];
  [fh logMessage:@"test 2" level:kGTMLoggerLevelInfo];
  [fh logMessage:@"test 3" level:kGTMLoggerLevelInfo];
  [fh closeFile];

  // Re-open file and make sure our log messages get appended
  fh = [NSFileHandle fileHandleForLoggingAtPath:path_ mode:0644];
  XCTAssertNotNil(fh);

  [fh logMessage:@"test 4" level:kGTMLoggerLevelInfo];
  [fh logMessage:@"test 5" level:kGTMLoggerLevelInfo];
  [fh logMessage:@"test 6" level:kGTMLoggerLevelInfo];
  [fh closeFile];

  NSError *err = nil;
  NSString *contents = [NSString stringWithContentsOfFile:path_
                                                 encoding:NSUTF8StringEncoding
                                                    error:&err];
  XCTAssertNotNil(contents, @"Error loading log file: %@", err);
  XCTAssertEqualObjects(@"test 1\ntest 2\ntest 3\ntest 4\ntest 5\ntest 6\n", contents);
}

@end
