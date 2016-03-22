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
