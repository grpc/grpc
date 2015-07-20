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

#import "GRXImmediateWriter.h"

#import "NSEnumerator+GRXUtil.h"

@implementation GRXImmediateWriter {
  NSEnumerator *_enumerator;
  NSError *_errorOrNil;
  id<GRXWriteable> _writeable;
}

@synthesize state = _state;

- (instancetype) init {
  return [self initWithEnumerator:nil error:nil]; // results in an empty writer.
}

// Designated initializer
- (instancetype)initWithEnumerator:(NSEnumerator *)enumerator error:(NSError *)errorOrNil {
  if (((self = [super init]))) {
    _enumerator = enumerator;
    _errorOrNil = errorOrNil;
    _state = GRXWriterStateNotStarted;
  }
  return self;
}

#pragma mark Convenience constructors

+ (instancetype)writerWithEnumerator:(NSEnumerator *)enumerator error:(NSError *)errorOrNil {
  return [[self alloc] initWithEnumerator:enumerator error:errorOrNil];
}

+ (GRXWriter *)writerWithEnumerator:(NSEnumerator *)enumerator {
  return [self writerWithEnumerator:enumerator error:nil];
}

+ (GRXWriter *)writerWithValueSupplier:(id (^)())block {
  return [self writerWithEnumerator:[NSEnumerator grx_enumeratorWithValueSupplier:block]];
}

+ (GRXWriter *)writerWithContainer:(id<NSFastEnumeration>)container {
  return [self writerWithEnumerator:[NSEnumerator grx_enumeratorWithContainer:container]];;
}

+ (GRXWriter *)writerWithValue:(id)value {
  return [self writerWithEnumerator:[NSEnumerator grx_enumeratorWithSingleValue:value]];
}

+ (GRXWriter *)writerWithError:(NSError *)error {
  return [self writerWithEnumerator:nil error:error];
}

+ (GRXWriter *)emptyWriter {
  return [self writerWithEnumerator:nil error:nil];
}

#pragma mark Conformance with GRXWriter

// Most of the complexity in this implementation is the result of supporting pause and resumption of
// the GRXWriter. It's an important feature for instances of GRXWriter that are backed by a
// container (which may be huge), or by a NSEnumerator (which may even be infinite).

- (void)writeUntilPausedOrStopped {
  id value;
  while (value = [_enumerator nextObject]) {
    [_writeable writeValue:value];
    // If the writeable has a reference to us, it might change our state to paused or finished.
    if (_state == GRXWriterStatePaused || _state == GRXWriterStateFinished) {
      return;
    }
  }
  [self finishWithError:_errorOrNil];
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  _state = GRXWriterStateStarted;
  _writeable = writeable;
  [self writeUntilPausedOrStopped];
}

- (void)finishWithError:(NSError *)errorOrNil {
  _state = GRXWriterStateFinished;
  _enumerator = nil;
  _errorOrNil = nil;
  id<GRXWriteable> writeable = _writeable;
  _writeable = nil;
  [writeable writesFinishedWithError:errorOrNil];
}

- (void)setState:(GRXWriterState)newState {
  // Manual transitions are only allowed from the started or paused states.
  if (_state == GRXWriterStateNotStarted || _state == GRXWriterStateFinished) {
    return;
  }

  switch (newState) {
    case GRXWriterStateFinished:
      _state = newState;
      _enumerator = nil;
      _errorOrNil = nil;
      // Per GRXWriter's contract, setting the state to Finished manually
      // means one doesn't wish the writeable to be messaged anymore.
      _writeable = nil;
      return;
    case GRXWriterStatePaused:
      _state = newState;
      return;
    case GRXWriterStateStarted:
      if (_state == GRXWriterStatePaused) {
        _state = newState;
        [self writeUntilPausedOrStopped];
      }
      return;
    case GRXWriterStateNotStarted:
      return;
  }
}

@end
