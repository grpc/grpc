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

#import "GRXBufferedPipe.h"

@implementation GRXBufferedPipe {
  id<GRXWriteable> _writeable;
  NSMutableArray *_queue;
  BOOL _inputIsFinished;
  NSError *_errorOrNil;
}

@synthesize state = _state;

+ (instancetype)pipe {
  return [[self alloc] init];
}

- (instancetype)init {
  if (self = [super init]) {
    _queue = [NSMutableArray array];
    _state = GRXWriterStateNotStarted;
  }
  return self;
}

- (id)popValue {
  id value = _queue[0];
  [_queue removeObjectAtIndex:0];
  return value;
}

- (void)writeBufferUntilPausedOrStopped {
  while (_state == GRXWriterStateStarted && _queue.count > 0) {
    [_writeable writeValue:[self popValue]];
  }
  if (_inputIsFinished && _queue.count == 0) {
    // Our writer finished normally while we were paused or not-started-yet.
    [self finishWithError:_errorOrNil];
  }
}

#pragma mark GRXWriteable implementation

// Returns whether events can be simply propagated to the other end of the pipe.
- (BOOL)shouldFastForward {
  return _state == GRXWriterStateStarted && _queue.count == 0;
}

- (void)writeValue:(id)value {
  if (self.shouldFastForward) {
    // Skip the queue.
    [_writeable writeValue:value];
  } else {
    // Even if we're paused and with enqueued values, we can't excert back-pressure to our writer.
    // So just buffer the new value.
    // We need a copy, so that it doesn't mutate before it's written at the other end of the pipe.
    if ([value respondsToSelector:@selector(copy)]) {
      value = [value copy];
    }
    [_queue addObject:value];
  }
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  _inputIsFinished = YES;
  _errorOrNil = errorOrNil;
  if (errorOrNil || self.shouldFastForward) {
    // No need to write pending values.
    [self finishWithError:_errorOrNil];
  }
}

#pragma mark GRXWriter implementation

- (void)setState:(GRXWriterState)newState {
  // Manual transitions are only allowed from the started or paused states.
  if (_state == GRXWriterStateNotStarted || _state == GRXWriterStateFinished) {
    return;
  }

  switch (newState) {
    case GRXWriterStateFinished:
      _state = newState;
      _queue = nil;
      // Per GRXWriter's contract, setting the state to Finished manually means one doesn't wish the
      // writeable to be messaged anymore.
      _writeable = nil;
      return;
    case GRXWriterStatePaused:
      _state = newState;
      return;
    case GRXWriterStateStarted:
      if (_state == GRXWriterStatePaused) {
        _state = newState;
        [self writeBufferUntilPausedOrStopped];
      }
      return;
    case GRXWriterStateNotStarted:
      return;
  }
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  _state = GRXWriterStateStarted;
  _writeable = writeable;
  [self writeBufferUntilPausedOrStopped];
}

- (void)finishWithError:(NSError *)errorOrNil {
  id<GRXWriteable> writeable = _writeable;
  self.state = GRXWriterStateFinished;
  [writeable writesFinishedWithError:errorOrNil];
}

@end
