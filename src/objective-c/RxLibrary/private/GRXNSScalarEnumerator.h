#import <Foundation/Foundation.h>

// Concrete subclass of NSEnumerator whose instances return a single object before finishing.
@interface GRXNSScalarEnumerator : NSEnumerator
// Param value: the single object this instance will produce. After the first invocation of
// nextObject, the value is released.
- (instancetype)initWithValue:(id)value;
@end
