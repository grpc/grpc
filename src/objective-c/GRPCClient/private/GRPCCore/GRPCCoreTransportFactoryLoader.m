
#import "GRPCCoreTransportFactoryLoader.h"

#import <GRPCClient/GRPCCall+Cronet.h>

#import "../GRPCTransport+Private.h"
#import "GRPCCoreFactory.h"

#define NSStringFromUTF8String(cstr) [NSString stringWithUTF8String:cstr]
#define TransportIdFromNSString(str) ((GRPCTransportID)[str UTF8String])

@implementation GRPCCoreTransportFactoryLoader

+ (instancetype)sharedInstance {
  static dispatch_once_t initOnce;
  static GRPCCoreTransportFactoryLoader *gTransportLoader = nil;
  dispatch_once(&initOnce, ^{
    gTransportLoader = [[GRPCCoreTransportFactoryLoader alloc] init];
    NSAssert(gTransportLoader != nil, @"Unable to initialize transport loader.");
  });
  return gTransportLoader;
}

- (NSDictionary<NSString *, Class> *)coreTransports {
  return @{
    NSStringFromUTF8String(GRPCDefaultTransportImplList.core_secure) :
        [GRPCCoreSecureFactory class],
    NSStringFromUTF8String(GRPCDefaultTransportImplList.core_insecure) :
        [GRPCCoreInsecureFactory class]
  };
}

- (void)loadCoreFactories {
  GRPCTransportRegistry *registry = [GRPCTransportRegistry sharedInstance];
  NSDictionary *coreTransports = [self coreTransports];
  for (NSString *transportId in coreTransports) {
    GRPCTransportID tid = TransportIdFromNSString(transportId);
    Class factoryCls = coreTransports[transportId];
    if (factoryCls && ![registry isRegisteredForTransportID:tid]) {
      [[GRPCTransportRegistry sharedInstance] registerTransportWithID:tid factory:[factoryCls new]];
    }
  }
}

@end
