#import "GRXWriter.h"

// A "proxy" writer that transforms all the values of its input writer by using a mapping function.
@interface GRXMappingWriter : GRXWriter
- (instancetype)initWithWriter:(id<GRXWriter>)writer map:(id (^)(id value))map
    NS_DESIGNATED_INITIALIZER;
@end
