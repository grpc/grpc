#import "GRXMappingWriter.h"

static id (^kIdentity)(id value) = ^id(id value) {
  return value;
};

@interface GRXWriter () <GRXWriteable>
@end

@implementation GRXMappingWriter {
  id (^_map)(id value);
}

- (instancetype)initWithWriter:(id<GRXWriter>)writer {
  return [self initWithWriter:writer map:nil];
}

// Designated initializer
- (instancetype)initWithWriter:(id<GRXWriter>)writer map:(id (^)(id value))map {
  if ((self = [super initWithWriter:writer])) {
    _map = map ?: kIdentity;
  }
  return self;
}

// Override
- (void)didReceiveValue:(id)value {
  [super didReceiveValue:_map(value)];
}
@end
