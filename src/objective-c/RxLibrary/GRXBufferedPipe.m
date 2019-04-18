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

@interface GRXBufferedPipe ()
@property(atomic) id<GRXWriteable> writeable;
@end

@implementation GRXBufferedPipe {
  NSError *_errorOrNil;
  dispatch_queue_t _writeQueue;
}

@synthesize state = _state;

+ (instancetype)pipe {
  return [[self alloc] init];
}

- (instancetype)init {
  if (self = [super init]) {
    _state = GRXWriterStateNotStarted;
    _writeQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    dispatch_suspend(_writeQueue);
  }
  return self;
}

#pragma mark GRXWriteable implementation

- (void)writeValue:(id)value {
  if ([value respondsToSelector:@selector(copy)]) {
    // Even if we're paused and with enqueued values, we can't excert back-pressure to our writer.
    // So just buffer the new value.
    // We need a copy, so that it doesn't mutate before it's written at the other end of the pipe.
    value = [value copy];
  }
  dispatch_async(_writeQueue, ^(void) {
    @synchronized(self) {
      if (self->_state == GRXWriterStateFinished) {
        return;
      }
      [self.writeable writeValue:value];
    }
  });
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  dispatch_async(_writeQueue, ^{
    if (self->_state == GRXWriterStateFinished) {
      return;
    }
    [self finishWithError:errorOrNil];
  });
}

#pragma mark GRXWriter implementation

- (void)setState:(GRXWriterState)newState {
  @synchronized(self) {
    // Manual transitions are only allowed from the started or paused states.
    if (_state == GRXWriterStateNotStarted || _state == GRXWriterStateFinished) {
      return;
    }

    switch (newState) {
      case GRXWriterStateFinished:
        self.writeable = nil;
        if (_state == GRXWriterStatePaused) {
          dispatch_resume(_writeQueue);
        }
        _state = newState;
        return;
      case GRXWriterStatePaused:
        if (_state == GRXWriterStateStarted) {
          _state = newState;
          dispatch_suspend(_writeQueue);
        }
        return;
      case GRXWriterStateStarted:
        if (_state == GRXWriterStatePaused) {
          _state = newState;
          dispatch_resume(_writeQueue);
        }
        return;
      case GRXWriterStateNotStarted:
        return;
    }
  }
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  @synchronized(self) {
    self.writeable = writeable;
    _state = GRXWriterStateStarted;
  }
  dispatch_resume(_writeQueue);
}

- (void)finishWithError:(NSError *)errorOrNil {
  [self.writeable writesFinishedWithError:errorOrNil];
}

- (void)dealloc {
  GRXWriterState state = self.state;
  if (state == GRXWriterStateNotStarted || state == GRXWriterStatePaused) {
    dispatch_resume(_writeQueue);
  }
}

@end
