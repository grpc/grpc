#import <grpc/grpc.h>

#pragma mark - Wrapped Channel Arguments

/**
 * A wrapper @c grpc_channel_args that frees allocated memory used to copy key / value pairs by the
 * @c GRPCWrappedChannelArgsBuilder.
 */
@interface GRPCWrappedChannelArgs : NSObject

@property(nonatomic, readonly) grpc_channel_args channelArgs;

- (instancetype)init NS_UNAVAILABLE;

@end

#pragma mark - Wrapped Channel Arguments Builder

/**
 * A builder that simplifies construction and usage of @c grpc_channel_args by building a
 * @c GRPCWrappedChannelArgs.
 */
@interface GRPCWrappedChannelArgsBuilder : NSObject

- (instancetype)addKey:(NSString *)key stringValue:(NSString *)value;
- (instancetype)addKey:(NSString *)key integerValue:(int)value;
- (GRPCWrappedChannelArgs *)build;

@end