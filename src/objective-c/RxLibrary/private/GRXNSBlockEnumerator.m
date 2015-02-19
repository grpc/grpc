#import "GRXNSBlockEnumerator.h"

@implementation GRXNSBlockEnumerator {
  id (^_block)();
}

- (instancetype)init {
  return [self initWithValueSupplier:nil];
}

- (instancetype)initWithValueSupplier:(id (^)())block {
  if ((self = [super init])) {
    _block = block;
  }
  return self;
}

- (id)nextObject {
  if (!_block) {
    return nil;
  }
  id value = _block();
  if (!value) {
    _block = nil;
  }
  return value;
}
@end
