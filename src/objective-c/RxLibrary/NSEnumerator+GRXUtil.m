#import "NSEnumerator+GRXUtil.h"

#import "private/GRXNSBlockEnumerator.h"
#import "private/GRXNSFastEnumerator.h"
#import "private/GRXNSScalarEnumerator.h"

@implementation NSEnumerator (GRXUtil)

+ (NSEnumerator *)grx_enumeratorWithContainer:(id<NSFastEnumeration>)container {
  // TODO(jcanizales): Consider checking if container responds to objectEnumerator and return that?
  return [[GRXNSFastEnumerator alloc] initWithContainer:container];
}

+ (NSEnumerator *)grx_enumeratorWithSingleValue:(id)value {
  return [[GRXNSScalarEnumerator alloc] initWithValue:value];
}

+ (NSEnumerator *)grx_enumeratorWithValueSupplier:(id (^)())block {
  return [[GRXNSBlockEnumerator alloc] initWithValueSupplier:block];
}
@end
