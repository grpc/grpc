#import "ProtoServiceLegacy.h"
#import "ProtoRPCLegacy.h"
#import "ProtoMethod.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
@implementation ProtoService (Legacy)
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-designated-initializers"
// Do not call designated initializer here due to nullability incompatibility. This method is from
// old API and does not assert on nullability of the parameters.

- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName {
  if ((self = [super init])) {
    _host = [host copy];
    _packageName = [packageName copy];
    _serviceName = [serviceName copy];
  }
  return self;
}


- (GRPCProtoCall *)RPCToMethod:(NSString *)method
                requestsWriter:(GRXWriter *)requestsWriter
                 responseClass:(Class)responseClass
            responsesWriteable:(id<GRXWriteable>)responsesWriteable {
  GRPCProtoMethod *methodName =
  [[GRPCProtoMethod alloc] initWithPackage:_packageName service:_serviceName method:method];
  return [[GRPCProtoCall alloc] initWithHost:_host
                                      method:methodName
                              requestsWriter:requestsWriter
                               responseClass:responseClass
                          responsesWriteable:responsesWriteable];
}

@end
