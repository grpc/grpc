#import "GRPCTransport.h"

static const GRPCTransportId gGRPCCoreSecureId = "io.grpc.transport.core.secure";
static const GRPCTransportId gGRPCCoreInsecureId = "io.grpc.transport.core.insecure";

const struct GRPCTransportImplList GRPCTransportImplList = {
  .core_secure = gGRPCCoreSecureId,
  .core_insecure = gGRPCCoreInsecureId};

static const GRPCTransportId gDefaultTransportId = gGRPCCoreSecureId;

static GRPCTransportRegistry *gTransportRegistry = nil;
static dispatch_once_t initTransportRegistry;

BOOL TransportIdIsEqual(GRPCTransportId lhs, GRPCTransportId rhs) {
  // Directly comparing pointers works because we require users to use the id provided by each
  // implementation, not coming up with their own string.
  return lhs == rhs;
}

NSUInteger TransportIdHash(GRPCTransportId transportId) {
  if (transportId == NULL) {
    transportId = gDefaultTransportId;
  }
  return [NSString stringWithCString:transportId encoding:NSUTF8StringEncoding].hash;
}

@implementation GRPCTransportRegistry {
  NSMutableDictionary<NSString *, id<GRPCTransportFactory>> *_registry;
  id<GRPCTransportFactory> _defaultFactory;
}

+ (instancetype)sharedInstance {
  dispatch_once(&initTransportRegistry, ^{
    gTransportRegistry = [[GRPCTransportRegistry alloc] init];
    NSAssert(gTransportRegistry != nil, @"Unable to initialize transport registry.");
    if (gTransportRegistry == nil) {
      NSLog(@"Unable to initialize transport registry.");
      [NSException raise:NSGenericException format:@"Unable to initialize transport registry."];
    }
  });
  return gTransportRegistry;
}

- (instancetype)init {
  if ((self = [super init])) {
    _registry = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)registerTransportWithId:(GRPCTransportId)transportId factory:(id<GRPCTransportFactory>)factory {
  NSString *nsTransportId = [NSString stringWithCString:transportId
                                                       encoding:NSUTF8StringEncoding];
  NSAssert(_registry[nsTransportId] == nil, @"The transport %@ has already been registered.", nsTransportId);
  if (_registry[nsTransportId] != nil) {
    NSLog(@"The transport %@ has already been registered.", nsTransportId);
    return;
  }
  _registry[nsTransportId] = factory;

  // if the default transport is registered, mark it.
  if (0 == strcmp(transportId, gDefaultTransportId)) {
    _defaultFactory = factory;
  }
}

- (id<GRPCTransportFactory>)getTransportFactoryWithId:(GRPCTransportId)transportId {
  if (transportId == NULL) {
    if (_defaultFactory == nil) {
      [NSException raise:NSInvalidArgumentException format:@"Unable to get default transport factory"];
      return nil;
    }
    return _defaultFactory;
  }
  NSString *nsTransportId = [NSString stringWithCString:transportId
                                               encoding:NSUTF8StringEncoding];
  id<GRPCTransportFactory> transportFactory = _registry[nsTransportId];
  if (transportFactory == nil) {
    // User named a transport id that was not registered with the registry.
    [NSException raise:NSInvalidArgumentException format:@"Unable to get transport factory with id %s", transportId];
    return nil;
  }
  return transportFactory;
}

@end

@implementation GRPCTransport

- (dispatch_queue_t)dispatchQueue {
  [NSException raise:NSGenericException format:@"Implementations should override the dispatch queue"];
  return nil;
}

- (void)startWithRequestOptions:(nonnull GRPCRequestOptions *)requestOptions callOptions:(nonnull GRPCCallOptions *)callOptions {
  [NSException raise:NSGenericException format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)writeData:(nonnull id)data {
  [NSException raise:NSGenericException format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)cancel {
  [NSException raise:NSGenericException format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)finish {
  [NSException raise:NSGenericException format:@"Implementations should override the methods of GRPCTransport"];
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  [NSException raise:NSGenericException format:@"Implementations should override the methods of GRPCTransport"];
}

@end
