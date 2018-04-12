/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
  BOOL _alreadyFinished;
}

- (instancetype)init {
  return [self initWithWriteable:nil];
}

// Designated initializer
- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable
                    dispatchQueue:(dispatch_queue_t)queue {
  if (self = [super init]) {
    _writeableQueue = queue;
    _writeable = writeable;
  }
  return self;
}

- (instancetype)initWithWriteable:(id<GRXWriteable>)writeable {
  return [self initWithWriteable:writeable dispatchQueue:dispatch_get_main_queue()];
}

- (void)enqueueValue:(id)value completionHandler:(void (^)(void))handler {
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
  __weak typeof(self) weakSelf = self;
  dispatch_async(_writeableQueue, ^{
    typeof(self) strongSelf = weakSelf;
    if (strongSelf) {
      BOOL finished = NO;
      @synchronized(self) {
        if (!strongSelf->_alreadyFinished) {
          strongSelf->_alreadyFinished = YES;
        } else {
          finished = YES;
        }
      }
      if (!finished) {
        // Cancellation is now impossible. None of the other three blocks can run concurrently with
        // this one.
        [self.writeable writesFinishedWithError:nil];
        // Skip any possible message to the wrapped writeable enqueued after this one.
        self.writeable = nil;
      }
    }
  });
}

- (void)cancelWithError:(NSError *)error {
  NSAssert(error, @"For a successful completion, use enqueueSuccessfulCompletion.");
  BOOL finished = NO;
  @synchronized(self) {
    if (!_alreadyFinished) {
      _alreadyFinished = YES;
    } else {
      finished = YES;
    }
  }
  if (!finished) {
    // Skip any of the still-enqueued messages to the wrapped writeable. We use the atomic setter to
    // nillify writeable because we might be running concurrently with the blocks in
    // _writeableQueue, and assignment with ARC isn't atomic.
    id<GRXWriteable> writeable = self.writeable;
    self.writeable = nil;

    dispatch_async(_writeableQueue, ^{
      [writeable writesFinishedWithError:error];
    });
  }
}

- (void)cancelSilently {
  BOOL finished = NO;
  @synchronized(self) {
    if (!_alreadyFinished) {
      _alreadyFinished = YES;
    } else {
      finished = YES;
    }
  }
  if (!finished) {
    // Skip any of the still-enqueued messages to the wrapped writeable. We use the atomic setter to
    // nillify writeable because we might be running concurrently with the blocks in
    // _writeableQueue, and assignment with ARC isn't atomic.
    self.writeable = nil;
  }
}
@end
