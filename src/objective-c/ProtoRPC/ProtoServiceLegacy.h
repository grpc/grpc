#import "ProtoService.h"

@class GRPCProtoCall;
@class GRXWriter;
@protocol GRXWriteable;

@interface ProtoService (Legacy)

- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName;

- (GRPCProtoCall *)RPCToMethod:(NSString *)method
                requestsWriter:(GRXWriter *)requestsWriter
                 responseClass:(Class)responseClass
            responsesWriteable:(id<GRXWriteable>)responsesWriteable;

@end
