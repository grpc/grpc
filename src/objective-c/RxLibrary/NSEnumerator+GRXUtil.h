#import <Foundation/Foundation.h>

@interface NSEnumerator (GRXUtil)

// Returns a NSEnumerator instance that iterates through the elements of the passed container that
// supports fast enumeration. Note that this negates the speed benefits of fast enumeration over
// NSEnumerator. It's only intended for the rare cases when one needs the latter and only has the
// former, e.g. for iteration that needs to be paused and resumed later.
+ (NSEnumerator *)grx_enumeratorWithContainer:(id<NSFastEnumeration>)container;

// Returns a NSEnumerator instance that provides a single object before finishing. The value is then
// released.
+ (NSEnumerator *)grx_enumeratorWithSingleValue:(id)value;

// Returns a NSEnumerator instance that delegates the invocations of nextObject to the passed block.
// When the block first returns nil, it is released.
+ (NSEnumerator *)grx_enumeratorWithValueSupplier:(id (^)())block;
@end
