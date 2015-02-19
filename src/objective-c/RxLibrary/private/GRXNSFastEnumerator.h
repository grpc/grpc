#import <Foundation/Foundation.h>

// This is a bridge to interact through NSEnumerator's interface with objects that only conform to
// NSFastEnumeration. (There's nothing specifically fast about it - you certainly don't win any
// speed by using this instead of a NSEnumerator provided by your container).
@interface GRXNSFastEnumerator : NSEnumerator
// After the iteration of the container (via the NSFastEnumeration protocol) is over, the container
// is released. If the container is modified during enumeration, an exception is thrown.
- (instancetype)initWithContainer:(id<NSFastEnumeration>)container;
@end
