#import "GRXWriter+Transformations.h"

#import "transformations/GRXMappingWriter.h"

@implementation GRXWriter (Transformations)

- (GRXWriter *)map:(id (^)(id))map {
  if (!map) {
    return self;
  }
  return [[GRXMappingWriter alloc] initWithWriter:self map:map];
}

@end
