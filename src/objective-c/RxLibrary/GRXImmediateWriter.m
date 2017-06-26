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
