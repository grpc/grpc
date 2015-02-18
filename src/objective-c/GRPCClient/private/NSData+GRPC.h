#import <Foundation/Foundation.h>

struct grpc_byte_buffer;

@interface NSData (GRPC)
+ (instancetype)grpc_dataWithByteBuffer:(struct grpc_byte_buffer *)buffer;
- (struct grpc_byte_buffer *)grpc_byteBuffer;
@end
