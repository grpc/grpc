#import "GRPCChannel.h"

#import <grpc.h>

@implementation GRPCChannel

+ (instancetype)channelToHost:(NSString *)host {
  // TODO(jcanizales): Reuse channels.
  return [[self alloc] initWithHost:host];
}

- (instancetype)init {
  return [self initWithHost:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host {
  if (!host) {
    [NSException raise:NSInvalidArgumentException format:@"Host can't be nil."];
  }
  if ((self = [super init])) {
    _unmanagedChannel = grpc_channel_create(host.UTF8String, NULL);
  }
  return self;
}

- (void)dealloc {
  // TODO(jcanizales): Be sure to add a test with a server that closes the connection prematurely,
  // as in the past that made this call to crash.
  grpc_channel_destroy(_unmanagedChannel);
}
@end
