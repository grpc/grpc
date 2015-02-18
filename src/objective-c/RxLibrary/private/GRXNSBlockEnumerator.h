#import <Foundation/Foundation.h>

// Concrete subclass of NSEnumerator that delegates the invocations of nextObject to a block passed
// on initialization.
@interface GRXNSBlockEnumerator : NSEnumerator
// The first time the passed block returns nil, the enumeration will end and the block will be
// released.
- (instancetype)initWithValueSupplier:(id (^)())block;
@end
