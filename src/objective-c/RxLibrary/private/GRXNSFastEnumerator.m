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

#import "GRXNSFastEnumerator.h"

@implementation GRXNSFastEnumerator {
  id<NSFastEnumeration> _container;
  NSFastEnumerationState _state;
  // Number of elements of the container currently in the _state.itemsPtr array.
  NSUInteger _count;
  // The index of the next object to return from the _state.itemsPtr array.
  NSUInteger _index;
  // A "buffer of one element," for the containers that enumerate their elements one by one. Those
  // will set _state.itemsPtr to point to this.
  // The NSFastEnumeration protocol requires it to be __unsafe_unretained, but that's alright
  // because the only use we'd make of its value is to return it immediately as the result of
  // nextObject.
  __unsafe_unretained id _bufferValue;
  // Neither NSEnumerator nor NSFastEnumeration instances are required to work correctly when the
  // underlying container is mutated during iteration. The expectation is that an exception is
  // thrown when that happens. So we check for mutations.
  unsigned long _mutationFlag;
  BOOL _mutationFlagIsSet;
}

- (instancetype)init {
  return [self initWithContainer:nil];
}

// Designated initializer.
- (instancetype)initWithContainer:(id<NSFastEnumeration>)container {
  if ((self = [super init])) {
    _container = container;
  }
  return self;
}

- (id)nextObject {
  if (_index == _count) {
    _index = 0;
    _count = [_container countByEnumeratingWithState:&_state objects:&_bufferValue count:1];
    if (_count == 0) {
      // Enumeration is over.
      _container = nil;
      return nil;
    }
    if (_mutationFlagIsSet) {
      NSAssert(_mutationFlag == *(_state.mutationsPtr),
               @"container was mutated while being enumerated");
    } else {
      _mutationFlag = *(_state.mutationsPtr);
      _mutationFlagIsSet = YES;
    }
  }
  return _state.itemsPtr[_index++];
}
@end
