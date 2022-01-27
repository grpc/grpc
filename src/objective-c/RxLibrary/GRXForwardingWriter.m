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

#import "GRXForwardingWriter.h"

@interface GRXForwardingWriter () <GRXWriteable>
@end

@implementation GRXForwardingWriter {
  GRXWriter *_writer;
  id<GRXWriteable> _writeable;
}

- (instancetype)init {
  return [self initWithWriter:nil];
}

// Designated initializer
- (instancetype)initWithWriter:(GRXWriter *)writer {
  if (!writer) {
    return nil;
  }
  if (writer.state != GRXWriterStateNotStarted) {
    [NSException raise:NSInvalidArgumentException
                format:@"The writer argument must not have already started."];
  }
  if ((self = [super init])) {
    _writer = writer;
  }
  return self;
}

// This is used to send a completion or an error to the writeable. It nillifies
// our reference to it in order to guarantee no more messages are sent to it,
// and to release it.
- (void)finishOutputWithError:(NSError *)errorOrNil {
  id<GRXWriteable> writeable = _writeable;
  _writeable = nil;
  [writeable writesFinishedWithError:errorOrNil];
}

#pragma mark GRXWriteable implementation

- (void)writeValue:(id)value {
  @synchronized(self) {
    [_writeable writeValue:value];
  }
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  @synchronized(self) {
    _writer = nil;
    [self finishOutputWithError:errorOrNil];
  }
}

#pragma mark GRXWriter implementation

- (GRXWriterState)state {
  GRXWriter *copiedWriter;
  @synchronized(self) {
    copiedWriter = _writer;
  }
  return copiedWriter ? copiedWriter.state : GRXWriterStateFinished;
}

- (void)setState:(GRXWriterState)state {
  GRXWriter *copiedWriter = nil;
  if (state == GRXWriterStateFinished) {
    @synchronized(self) {
      _writeable = nil;
      copiedWriter = _writer;
      _writer = nil;
    }
    copiedWriter.state = GRXWriterStateFinished;
  } else {
    @synchronized(self) {
      copiedWriter = _writer;
    }
    copiedWriter.state = state;
  }
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  GRXWriter *copiedWriter = nil;
  @synchronized(self) {
    _writeable = writeable;
    copiedWriter = _writer;
  }
  [copiedWriter startWithWriteable:self];
}

- (void)finishWithError:(NSError *)errorOrNil {
  GRXWriter *copiedWriter = nil;
  @synchronized(self) {
    [self finishOutputWithError:errorOrNil];
    copiedWriter = _writer;
    _writer = nil;
  }
  copiedWriter.state = GRXWriterStateFinished;
}

@end
