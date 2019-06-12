//
//  GTMLoggerRingBufferWriterTest.m
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

#import "GTMSenTestCase.h"
#import "GTMLoggerRingBufferWriter.h"
#import "GTMLogger.h"

// --------------------------------------------------
// CountingWriter keeps a count of the number of times it has been
// told to write something, and also keeps track of what it was
// asked to log.

@interface CountingWriter : NSObject<GTMLogWriter> {
 @private
  NSMutableArray *loggedContents_;
}

- (NSUInteger)count;
- (NSArray *)loggedContents;
- (void)reset;

@end  // CountingWriter

@implementation CountingWriter
- (void)logMessage:(NSString *)msg level:(GTMLoggerLevel)level {
  if (!loggedContents_) {
    loggedContents_ = [[NSMutableArray alloc] init];
  }
  [loggedContents_ addObject:msg];
}  // logMessage

- (void)dealloc {
  [loggedContents_ release];
  [super dealloc];
}  // dealloc

- (void)reset {
  [loggedContents_ release];
  loggedContents_ = nil;
}  // reset

- (NSUInteger)count {
  return [loggedContents_ count];
}  // count

- (NSArray *)loggedContents {
  return loggedContents_;
}  // loggedContents

@end  // CountingWriter


@interface GTMLoggerRingBufferWriterTest : GTMTestCase {
 @private
  GTMLogger *logger_;
  CountingWriter *countingWriter_;
}
@end  // GTMLoggerRingBufferWriterTest


// --------------------------------------------------

@implementation GTMLoggerRingBufferWriterTest

// Utilty to compare the set of messages captured by a CountingWriter
// with an array of expected messages.  The messages are expected to
// be in the same order in both places.

- (void)compareWriter:(CountingWriter *)writer
  withExpectedLogging:(NSArray *)expected
                 line:(int)line {
  NSArray *loggedContents = [writer loggedContents];

  XCTAssertEqual([expected count], [loggedContents count],
                 @"count mismatch from line %d", line);

  for (unsigned int i = 0; i < [expected count]; i++) {
    XCTAssertEqualObjects([expected objectAtIndex:i],
                          [loggedContents objectAtIndex:i],
                          @"logging mistmatch at index %d from line %d",
                          i, line);
  }

}  // compareWithExpectedLogging


- (void)setUp {
  countingWriter_ = [[CountingWriter alloc] init];
  logger_ = [[GTMLogger alloc] init];
}  // setUp


- (void)tearDown {
  [countingWriter_ release];
  [logger_ release];
}  // tearDown


- (void)testCreation {

  // Make sure initializers work.
  GTMLoggerRingBufferWriter *writer =
    [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:32
                                                     writer:countingWriter_];
  XCTAssertEqual([writer capacity], (NSUInteger)32);
  XCTAssertTrue([writer writer] == countingWriter_);
  XCTAssertEqual([writer count], (NSUInteger)0);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([writer totalLogged], (NSUInteger)0);

  // Try with invalid arguments.  Should always get nil back.
  writer =
    [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:0
                                                     writer:countingWriter_];
  XCTAssertNil(writer);

  writer = [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:32
                                                             writer:nil];
  XCTAssertNil(writer);

  writer = [[GTMLoggerRingBufferWriter alloc] init];
  XCTAssertNil(writer);

}  // testCreation


- (void)testLogging {
  GTMLoggerRingBufferWriter *writer =
    [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:4
                                                     writer:countingWriter_];
  [logger_ setWriter:writer];

  // Shouldn't do anything if there are no contents.
  [writer dumpContents];
  XCTAssertEqual([writer count], (NSUInteger)0);
  XCTAssertEqual([countingWriter_ count], (NSUInteger)0);

  // Log a single item.  Make sure the counts are accurate.
  [logger_ logDebug:@"oop"];
  XCTAssertEqual([writer count], (NSUInteger)1);
  XCTAssertEqual([writer totalLogged], (NSUInteger)1);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([countingWriter_ count], (NSUInteger)0);

  // Log a second item.  Also make sure counts are accurate.
  [logger_ logDebug:@"ack"];
  XCTAssertEqual([writer count], (NSUInteger)2);
  XCTAssertEqual([writer totalLogged], (NSUInteger)2);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([countingWriter_ count], (NSUInteger)0);

  // Print them, and make sure the countingWriter sees the right stuff.
  [writer dumpContents];
  XCTAssertEqual([countingWriter_ count], (NSUInteger)2);
  XCTAssertEqual([writer count], (NSUInteger)2);  // Should not be zeroed.
  XCTAssertEqual([writer totalLogged], (NSUInteger)2);

  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObjects:@"oop",@"ack", nil]
                 line:__LINE__];


  // Wipe the slates clean.
  [writer reset];
  [countingWriter_ reset];
  XCTAssertEqual([writer count], (NSUInteger)0);
  XCTAssertEqual([writer totalLogged], (NSUInteger)0);

  // An error log level should print the buffer and empty it.
  [logger_ logDebug:@"oop"];
  [logger_ logInfo:@"ack"];
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([writer totalLogged], (NSUInteger)2);

  [logger_ logError:@"blargh"];
  XCTAssertEqual([countingWriter_ count], (NSUInteger)3);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);

  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObjects:@"oop", @"ack",
                                     @"blargh", nil]
                 line:__LINE__];


  // An assert log level should do the same.  This also fills the
  // buffer to its limit without wrapping.
  [countingWriter_ reset];
  [logger_ logDebug:@"oop"];
  [logger_ logInfo:@"ack"];
  [logger_ logDebug:@"blargh"];
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([writer count], (NSUInteger)3);
  XCTAssertEqual([writer totalLogged], (NSUInteger)3);

  [logger_ logAssert:@"ouch"];
  XCTAssertEqual([countingWriter_ count], (NSUInteger)4);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObjects:@"oop", @"ack",
                                     @"blargh", @"ouch", nil]
                 line:__LINE__];


  // Try with exactly one wrap around.
  [countingWriter_ reset];
  [logger_ logInfo:@"ack"];
  [logger_ logDebug:@"oop"];
  [logger_ logDebug:@"blargh"];
  [logger_ logDebug:@"flong"];  // Fills buffer
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)0);
  XCTAssertEqual([writer count], (NSUInteger)4);

  [logger_ logAssert:@"ouch"];  // should drop "ack"
  XCTAssertEqual([countingWriter_ count], (NSUInteger)4);

  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObjects:@"oop", @"blargh",
                                     @"flong", @"ouch", nil]
                 line:__LINE__];


  // Try with more than one wrap around.
  [countingWriter_ reset];
  [logger_ logInfo:@"ack"];
  [logger_ logDebug:@"oop"];
  [logger_ logDebug:@"blargh"];
  [logger_ logDebug:@"flong"];  // Fills buffer
  [logger_ logDebug:@"bloogie"];  // should drop "ack"
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)1);
  XCTAssertEqual([writer count], (NSUInteger)4);

  [logger_ logAssert:@"ouch"];  // should drop "oop"
  XCTAssertEqual([countingWriter_ count], (NSUInteger)4);

  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObjects:@"blargh",
                                     @"flong", @"bloogie", @"ouch", nil]
                 line:__LINE__];
}  // testBasics


- (void)testCornerCases {
  // make sure we work with small buffer sizes.

  GTMLoggerRingBufferWriter *writer =
    [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:1
                                                     writer:countingWriter_];
  [logger_ setWriter:writer];

  [logger_ logInfo:@"ack"];
  XCTAssertEqual([countingWriter_ count], (NSUInteger)0);
  XCTAssertEqual([writer count], (NSUInteger)1);
  [writer dumpContents];
  XCTAssertEqual([countingWriter_ count], (NSUInteger)1);

  [self compareWriter:countingWriter_
        withExpectedLogging:[NSArray arrayWithObject:@"ack"]
                 line:__LINE__];

  [logger_ logDebug:@"oop"];  // should drop "ack"
  XCTAssertEqual([writer count], (NSUInteger)1);
  XCTAssertEqual([writer droppedLogCount], (NSUInteger)1);

  [countingWriter_ reset];
  [logger_ logError:@"snoogy"];  // should drop "oop"
  XCTAssertEqual([countingWriter_ count], (NSUInteger)1);

  [self compareWriter:countingWriter_
  withExpectedLogging:[NSArray arrayWithObjects:@"snoogy", nil]
                 line:__LINE__];

}  // testCornerCases



// Run 10 threads, all logging through the same logger.

static volatile NSUInteger gStoppedThreads = 0; // Total number that have stopped.

- (void)bangMe:(id)info {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  GTMLogger *logger = (GTMLogger *)info;

  // Log a string.
  for (int i = 0; i < 27; i++) {
    [logger logDebug:@"oop"];
  }

  // log another string which should push the first string out.
  // if we see any "oop"s in the logger output, then we know it got
  // confused.
  for (int i = 0; i < 15; i++) {
    [logger logDebug:@"ack"];
  }

  [pool release];
  @synchronized ([self class]) {
    gStoppedThreads++;
  }

}  // bangMe


- (void)testThreading {
  const NSUInteger kThreadCount = 10;
  const NSUInteger kCapacity = 10;

  GTMLoggerRingBufferWriter *writer =
    [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:kCapacity
                                                     writer:countingWriter_];
  [logger_ setWriter:writer];

  for (NSUInteger i = 0; i < kThreadCount; i++) {
    [NSThread detachNewThreadSelector:@selector(bangMe:)
                             toTarget:self
                           withObject:logger_];
  }

  // The threads are running, so wait for them all to finish.
  while (1) {
    NSDate *quick = [NSDate dateWithTimeIntervalSinceNow:0.2];
    [[NSRunLoop currentRunLoop] runUntilDate:quick];
    @synchronized ([self class]) {
      if (gStoppedThreads == kThreadCount) break;
    }
  }

  // Now make sure we get back what's expected.
  XCTAssertEqual([writer count], kThreadCount);
  XCTAssertEqual([countingWriter_ count], (NSUInteger)0);  // Nothing should be logged
  XCTAssertEqual([writer totalLogged], (NSUInteger)420);

  [logger_ logError:@"bork"];
  XCTAssertEqual([countingWriter_ count], kCapacity);

  NSArray *expected = [NSArray arrayWithObjects:
                       @"ack", @"ack", @"ack", @"ack", @"ack",
                       @"ack", @"ack", @"ack", @"ack", @"bork",
                       nil];
  [self compareWriter:countingWriter_
  withExpectedLogging:expected
                 line:__LINE__];

}  // testThreading

@end  // GTMLoggerRingBufferWriterTest
