//
//  GTMLoggerRingBufferWriter.m
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

#import "GTMLoggerRingBufferWriter.h"

// Holds a message and a level.
struct GTMRingBufferPair {
  // Explicitly using CFStringRef instead of NSString because in a GC world, the
  // NSString will be collected because there is no way for the GC to know that
  // there is a strong reference to the NSString in this data structure. By
  // using a CFStringRef we can CFRetain it, and avoid the problem.
  CFStringRef logMessage_;
  GTMLoggerLevel level_;
};


// There are two operations that involve iterating over the buffer
// contents and doing Something to them.  This is a callback function
// that is called for every pair living in the buffer.
typedef void (GTMRingBufferPairCallback)(GTMLoggerRingBufferWriter *rbw,
                                         GTMRingBufferPair *pair);


@interface GTMLoggerRingBufferWriter (PrivateMethods)

// Add the message and level to the ring buffer.
- (void)addMessage:(NSString *)message level:(GTMLoggerLevel)level;

// Walk the buffer invoking the callback.
- (void)iterateBufferWithCallback:(GTMRingBufferPairCallback)callback;

@end  // PrivateMethods


@implementation GTMLoggerRingBufferWriter

+ (id)ringBufferWriterWithCapacity:(NSUInteger)capacity
                            writer:(id<GTMLogWriter>)writer {
  GTMLoggerRingBufferWriter *rbw =
    [[[self alloc] initWithCapacity:capacity
                            writer:writer] autorelease];
  return rbw;

}  // ringBufferWriterWithCapacity


- (id)initWithCapacity:(NSUInteger)capacity
                writer:(id<GTMLogWriter>)writer {
  if ((self = [super init])) {
    writer_ = [writer retain];
    capacity_ = capacity;

    // iVars are initialized to NULL.
    // Calling calloc with 0 is outside the standard.
    if (capacity_) {
      buffer_ = (GTMRingBufferPair *)calloc(capacity_,
                                            sizeof(GTMRingBufferPair));
    }

    nextIndex_ = 0;

    if (capacity_ == 0 || !buffer_ || !writer_) {
      [self release];
      self = nil;
    }
  }
  return self;

}  // initWithCapacity


- (id)init {
  return [self initWithCapacity:0 writer:nil];
}  // init


- (void)dealloc {
  [self reset];

  [writer_ release];
  if (buffer_) free(buffer_);

  [super dealloc];

}  // dealloc


- (NSUInteger)capacity {
  return capacity_;
}  // capacity


- (id<GTMLogWriter>)writer {
  return writer_;
}  // writer


- (NSUInteger)count {
  NSUInteger count = 0;
  @synchronized(self) {
    if ((nextIndex_ == 0 && totalLogged_ > 0)
        || totalLogged_ >= capacity_) {
      // We've wrapped around
      count = capacity_;
    } else {
      count = nextIndex_;
    }
  }

  return count;

}  // count


- (NSUInteger)droppedLogCount {
  NSUInteger droppedCount = 0;

  @synchronized(self) {
    if (capacity_ > totalLogged_) {
      droppedCount = 0;
    } else {
      droppedCount = totalLogged_ - capacity_;
    }
  }

  return droppedCount;

}  // droppedLogCount


- (NSUInteger)totalLogged {
  return totalLogged_;
}  // totalLogged


// Assumes caller will do any necessary synchronization.
// This walks over the buffer, taking into account any wrap-around,
// and calls the callback on each pair.
- (void)iterateBufferWithCallback:(GTMRingBufferPairCallback)callback {
  GTMRingBufferPair *scan, *stop;

  // If we've wrapped around, print out the ring buffer from |nextIndex_|
  // to the end.
  if (totalLogged_ >= capacity_) {
    scan = buffer_ + nextIndex_;
    stop = buffer_ + capacity_;
    while (scan < stop) {
      callback(self, scan);
      ++scan;
    }
  }

  // And then print from the beginning to right before |nextIndex_|
  scan = buffer_;
  stop = buffer_ + nextIndex_;
  while (scan < stop) {
    callback(self, scan);
    ++scan;
  }

}  // iterateBufferWithCallback


// Used when resetting the buffer.  This frees the string and zeros out
// the structure.
static void ResetCallback(GTMLoggerRingBufferWriter *rbw,
                          GTMRingBufferPair *pair) {
  if (pair->logMessage_) {
    CFRelease(pair->logMessage_);
  }
  pair->logMessage_ = nil;
  pair->level_ = kGTMLoggerLevelUnknown;
}  // ResetCallback


// Reset the contents.
- (void)reset {
  @synchronized(self) {
    [self iterateBufferWithCallback:ResetCallback];
    nextIndex_ = 0;
    totalLogged_ = 0;
  }

}  // reset


// Go ahead and log the stored backlog, writing it through the
// ring buffer's |writer_|.
static void PrintContentsCallback(GTMLoggerRingBufferWriter *rbw,
                                  GTMRingBufferPair *pair) {
  [[rbw writer] logMessage:(NSString*)pair->logMessage_ level:pair->level_];
}  // PrintContentsCallback


- (void)dumpContents {
  @synchronized(self) {
    [self iterateBufferWithCallback:PrintContentsCallback];
  }
}  // printContents


// Assumes caller will do any necessary synchronization.
- (void)addMessage:(NSString *)message level:(GTMLoggerLevel)level {
  NSUInteger newIndex = nextIndex_;
  nextIndex_ = (nextIndex_ + 1) % capacity_;

  ++totalLogged_;

  // Now store the goodies.
  GTMRingBufferPair *pair = buffer_ + newIndex;
  if (pair->logMessage_) {
    CFRelease(pair->logMessage_);
    pair->logMessage_ = nil;
  }
  if (message) {
    pair->logMessage_ = CFStringCreateCopy(kCFAllocatorDefault, (CFStringRef)message);
  }
  pair->level_ = level;

}  // addMessage


// From the GTMLogWriter protocol.
- (void)logMessage:(NSString *)message level:(GTMLoggerLevel)level {
  @synchronized(self) {
    [self addMessage:(NSString*)message level:level];

    if (level >= kGTMLoggerLevelError) {
      [self dumpContents];
      [self reset];
    }
  }

}  // logMessage

@end  // GTMLoggerRingBufferWriter
