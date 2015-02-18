#import <Foundation/Foundation.h>

struct grpc_metadata;

@interface NSDictionary (GRPC)
+ (instancetype)grpc_dictionaryFromMetadata:(struct grpc_metadata *)entries count:(size_t)count;
@end
