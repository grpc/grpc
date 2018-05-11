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

@interface GRXForwardingWriter ()<GRXWriteable>
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

// This is used to stop the input writer. It nillifies our reference to it
// to release it.
- (void)finishInput {
  GRXWriter *writer = _writer;
  _writer = nil;
  writer.state = GRXWriterStateFinished;
}

#pragma mark GRXWriteable implementation

- (void)writeValue:(id)value {
  [_writeable writeValue:value];
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  _writer = nil;
  [self finishOutputWithError:errorOrNil];
}

#pragma mark GRXWriter implementation

- (GRXWriterState)state {
  return _writer ? _writer.state : GRXWriterStateFinished;
}

- (void)setState:(GRXWriterState)state {
  if (state == GRXWriterStateFinished) {
    _writeable = nil;
    [self finishInput];
  } else {
    _writer.state = state;
  }
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  _writeable = writeable;
  [_writer startWithWriteable:self];
}

- (void)finishWithError:(NSError *)errorOrNil {
  [self finishOutputWithError:errorOrNil];
  [self finishInput];
}

@end
