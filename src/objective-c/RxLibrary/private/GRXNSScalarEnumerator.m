#import "GRXNSScalarEnumerator.h"

@implementation GRXNSScalarEnumerator {
  id _value;
}

- (instancetype)init {
  return [self initWithValue:nil];
}

// Designated initializer.
- (instancetype)initWithValue:(id)value {
  if ((self = [super init])) {
    _value = value;
  }
  return self;
}

- (id)nextObject {
  id value = _value;
  _value = nil;
  return value;
}
@end
