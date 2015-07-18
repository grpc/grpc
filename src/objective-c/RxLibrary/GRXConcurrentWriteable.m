/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import "GRXConcurrentWriteable.h"

#import <RxLibrary/GRXWriteable.h>

@interface GRXConcurrentWriteable ()
// This is atomic so that cancellation can nillify it from any thread.
@property(atomic, strong) id<GRXWriteable> writeable;
@end

@implementation GRXConcurrentWriteable {
  dispatch_queue_t _writeableQueue;
  // This ensures that writesFinishedWithError: is only sent once to the writeable.
  dispatch_once_t _alreadyFinished;
}

- (instancetype)init {
  return [self initWithWriteable:nil];
}

// Designated initializer
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable {
  if (self = [super init]) {
    _writeableQueue = dispatch_get_main_queue();
    _writeable = writeable;
  }
  return self;
}

- (void)enqueueValue:(id)value completionHandler:(void (^)())handler {
  dispatch_async(_writeableQueue, ^{
    // We're racing a possible cancellation performed by another thread. To turn all already-
    // enqueued messages into noops, cancellation nillifies the writeable property. If we get it
    // before it's nil, we won the race.
    id<GRXWriteable> writeable = self.writeable;
    if (writeable) {
      [writeable writeValue:value];
      handler();
    }
  });
}

- (void)enqueueSuccessfulCompletion {
  dispatch_async(_writeableQueue, ^{
    dispatch_once(&_alreadyFinished, ^{
      // Cancellation is now impossible. None of the other three blocks can run concurrently with
      // this one.
      [self.writeable writesFinishedWithError:nil];
      // Skip any possible message to the wrapped writeable enqueued after this one.
      self.writeable = nil;
    });
  });
}

- (void)cancelWithError:(NSError *)error {
  NSAssert(error, @"For a successful completion, use enqueueSuccessfulCompletion.");
  dispatch_once(&_alreadyFinished, ^{
    // Skip any of the still-enqueued messages to the wrapped writeable. We use the atomic setter to
    // nillify writeable because we might be running concurrently with the blocks in
    // _writeableQueue, and assignment with ARC isn't atomic.
    id<GRXWriteable> writeable = self.writeable;
    self.writeable = nil;

    dispatch_async(_writeableQueue, ^{
      [writeable writesFinishedWithError:error];
    });
  });
}

- (void)cancelSilently {
  dispatch_once(&_alreadyFinished, ^{
    // Skip any of the still-enqueued messages to the wrapped writeable. We use the atomic setter to
    // nillify writeable because we might be running concurrently with the blocks in
    // _writeableQueue, and assignment with ARC isn't atomic.
    self.writeable = nil;
  });
}
@end
