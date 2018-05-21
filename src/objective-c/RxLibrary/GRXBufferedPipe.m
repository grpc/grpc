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

typedef void (^GRXBufferedPipeCompletionHandler)(void);

@interface GRXBufferedPipeWriteOperation : NSObject
@property(nonatomic, strong) id value;
@property(nonatomic, strong) GRXBufferedPipeCompletionHandler completionHandler;
@end

@implementation GRXBufferedPipeWriteOperation
@end

@implementation GRXBufferedPipe {
  BOOL _writing;
  NSMutableArray *_queue;
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
    _writing = NO;
    _queue = [NSMutableArray array];
    dispatch_suspend(_writeQueue);
  }
  return self;
}

#pragma mark GRXWriteable implementation

- (void)writeValue:(id)value {
  [self writeValue:value completionHandler:nil];
}

- (void)writeValue:(id)value completionHandler:(void (^)(void))completionHandler {
  if ([value respondsToSelector:@selector(copy)]) {
    // Even if we're paused and with enqueued values, we can't excert back-pressure to our writer.
    // So just buffer the new value.
    // We need a copy, so that it doesn't mutate before it's written at the other end of the pipe.
    value = [value copy];
  }

  GRXBufferedPipeWriteOperation *writeOp = [[GRXBufferedPipeWriteOperation alloc] init];
  writeOp.value = value;
  writeOp.completionHandler = completionHandler;

  @synchronized(self) {
    if (_writing) {
      [_queue addObject:writeOp];
      return;
    } else {
      _writing = YES;
    }
  }
  [self startWriteOperation:writeOp];
}

/**
 * grpc_call allows only one operation by each op type.
 * Otherwise, grpc_call will raise an error. 
 * To avoid the error, use a queue and completion handler to perform operations one by one.
**/
- (void)startWriteOperation:(GRXBufferedPipeWriteOperation *)writeOp {
  __weak GRXBufferedPipe *weakSelf = self;
  GRXBufferedPipeCompletionHandler completionHandler = writeOp.completionHandler;
  dispatch_async(_writeQueue, ^(void) {
    [weakSelf.writeable writeValue:writeOp.value completionHandler:^(void) {
      if (completionHandler) {
        completionHandler();
      }

      GRXBufferedPipe *strongSelf = weakSelf;
      if (strongSelf) {
        GRXBufferedPipeWriteOperation *nextWriteOp;
        @synchronized(strongSelf) {
          if (strongSelf->_queue.count == 0) {
            strongSelf->_writing = NO;
            return;
          }
          nextWriteOp = strongSelf->_queue[0];
          [strongSelf->_queue removeObjectAtIndex:0];
        }
        [strongSelf startWriteOperation:nextWriteOp];
      }
    }];
  });
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  __weak GRXBufferedPipe *weakSelf = self;
  dispatch_async(_writeQueue, ^{
    [weakSelf finishWithError:errorOrNil];
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
  self.writeable = writeable;
  _state = GRXWriterStateStarted;
  dispatch_resume(_writeQueue);
}

- (void)finishWithError:(NSError *)errorOrNil {
  [self.writeable writesFinishedWithError:errorOrNil];
  self.state = GRXWriterStateFinished;
}

- (void)dealloc {
  GRXWriterState state = self.state;
  if (state == GRXWriterStateNotStarted || state == GRXWriterStatePaused) {
    [_queue removeAllObjects];
    dispatch_resume(_writeQueue);
  }
}

@end
