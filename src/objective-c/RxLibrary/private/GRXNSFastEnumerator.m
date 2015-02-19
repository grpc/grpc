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
  NSAssert(container, @"container can't be nil");
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
