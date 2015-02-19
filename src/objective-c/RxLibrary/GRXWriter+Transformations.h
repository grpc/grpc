#import "GRXWriter.h"

@interface GRXWriter (Transformations)

// Returns a writer that wraps the receiver, and has all the values the receiver would write
// transformed by the provided mapping function.
- (GRXWriter *)map:(id (^)(id value))map;

@end
