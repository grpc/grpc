/*
 *
 * Copyright 2016, Google Inc.
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

#import "GRXImmediateSingleWriter.h"

@implementation GRXImmediateSingleWriter {
  id _value;
  NSError *_errorOrNil;
  id<GRXWriteable> _writeable;
}

@synthesize state = _state;

- (instancetype)initWithValue:(id)value error:(NSError *)errorOrNil {
  if (self = [super init]) {
    _value = value;
    _errorOrNil = errorOrNil;
    _state = GRXWriterStateNotStarted;
  }
  return self;
}

+ (GRXWriter *)writerWithValue:(id)value {
  return [[self alloc] initWithValue:value error:nil];
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  _state = GRXWriterStateStarted;
  _writeable = writeable;
  [writeable writeValue:_value];
  [self finishWithError:_errorOrNil];
}

- (void)finishWithError:(NSError *)errorOrNil {
  _state = GRXWriterStateFinished;
  _errorOrNil = nil;
  _value = nil;
  id<GRXWriteable> writeable = _writeable;
  _writeable = nil;
  [writeable writesFinishedWithError:errorOrNil];
}

- (void)setState:(GRXWriterState)newState {
  // Manual state transition is not allowed
  return;
}

- (GRXWriter *)map:(id (^)(id))map {
  _value = map(_value);
  return self;
}

@end