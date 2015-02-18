#import "NSDictionary+GRPC.h"

#include <grpc.h>

@implementation NSDictionary (GRPC)
+ (instancetype)grpc_dictionaryFromMetadata:(grpc_metadata *)entries count:(size_t)count {
  NSMutableDictionary *metadata = [NSMutableDictionary dictionaryWithCapacity:count];
  for (grpc_metadata *entry = entries; entry < entries + count; entry++) {
    // TODO(jcanizales): Verify in a C library test that it's converting header names to lower case automatically.
    NSString *name = [NSString stringWithUTF8String:entry->key];
    if (!name) {
      continue;
    }
    if (!metadata[name]) {
      metadata[name] = [NSMutableArray array];
    }
    // TODO(jcanizales): Should we use a non-copy constructor?
    [metadata[name] addObject:[NSData dataWithBytes:entry->value
                                             length:entry->value_length]];
  }
  return metadata;
}
@end
