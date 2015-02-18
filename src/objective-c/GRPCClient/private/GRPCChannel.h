#import <Foundation/Foundation.h>

struct grpc_channel;

// Each separate instance of this class represents at least one TCP
// connection to the provided host. To create a grpc_call, pass the
// value of the unmanagedChannel property to grpc_channel_create_call.
// Release this object when the call is finished.
@interface GRPCChannel : NSObject
@property(nonatomic, readonly) struct grpc_channel *unmanagedChannel;

// Convenience constructor to allow for reuse of connections.
+ (instancetype)channelToHost:(NSString *)host;

// Designated initializer
- (instancetype)initWithHost:(NSString *)host;
@end
