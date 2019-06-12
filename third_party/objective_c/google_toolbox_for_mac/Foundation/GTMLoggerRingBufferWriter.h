//
//  GTMLoggerRingBufferWriter.h
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
#import "GTMDefines.h"

typedef struct GTMRingBufferPair GTMRingBufferPair;

// GTMLoggerRingBufferWriter is a GTMLogWriter that accumulates logged Info
// and Debug messages (when they're not compiled out in a release build)
// into a ring buffer.  If an Error or Assert message is
// logged, all of the previously logged messages (up to the size of the
// buffer) are then logged.  At that point the buffer resets itself.
//
// How to use:
//
// * Create a logger writer that you want to use to do the ultimate writing,
//   such as to stdErr, or a log file, or an NSArray that aggregates other
//   writers.
//   id<GTMLoggerWriter> someWriter = ...
//
// * Make a new ring buffer with this writer, along with the buffer's
//   capacity (which must be >= 1):
//     rbw =
//         [GTMLoggerRingBufferWriter ringBufferWriterWithCapacity:32
//                                                          writer:someWriter];
//
// * Set your logger's writer to be the ring buffer writer:
//    [[GTMLogger sharedLogger] setWriter:rbw];
//
// Note that this writer is at the end of the GTMLogger food chain, where the
// default filter removes Info messages in Release mode (Debug messages are
// compiled out).  You can pass nil to GTMLogger's -setFilter to have it pass
// along all the messages.
//
@interface GTMLoggerRingBufferWriter : NSObject <GTMLogWriter> {
 @private
  id<GTMLogWriter> writer_;
  GTMRingBufferPair *buffer_;
  NSUInteger capacity_;
  NSUInteger nextIndex_;    // Index of the next element of |buffer_| to fill.
  NSUInteger totalLogged_;  // This > 0 and |nextIndex_| == 0 means we've wrapped.
}

// Returns an autoreleased ring buffer writer.  If |writer| is nil,
// then nil is returned.
+ (id)ringBufferWriterWithCapacity:(NSUInteger)capacity
                            writer:(id<GTMLogWriter>)loggerWriter;

// Designated initializer.  If |writer| is nil, then nil is returned.
// If you just use -init, nil will be returned.
- (id)initWithCapacity:(NSUInteger)capacity
                writer:(id<GTMLogWriter>)loggerWriter;

// How many messages will be logged before older messages get dropped
// on the floor.
- (NSUInteger)capacity;

// The log writer that will get the buffered log messages if/when they
// need to be displayed.
- (id<GTMLogWriter>)writer;

// How many log messages are currently in the buffer.
- (NSUInteger)count;

// How many have been dropped on the floor since creation, or the last
// reset.
- (NSUInteger)droppedLogCount;

// The total number of messages processed since creation, or the last
// reset.
- (NSUInteger)totalLogged;

// Purge the contents and reset the counters.
- (void)reset;

// Print out the contents without resetting anything.
// Contents are automatically printed and reset when an error-level
// message comes through.
- (void)dumpContents;

@end  // GTMLoggerRingBufferWriter
