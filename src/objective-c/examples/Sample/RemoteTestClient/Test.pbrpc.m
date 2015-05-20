#import "Test.pbrpc.h"
#import <gRPC/GRXWriteable.h>
#import <gRPC/GRXWriter+Immediate.h>
#import <gRPC/ProtoRPC.h>

static NSString *const kPackageName = @"grpc.testing";
static NSString *const kServiceName = @"TestService";

@implementation RMTTestService

// Designated initializer
- (instancetype)initWithHost:(NSString *)host {
  return (self = [super initWithHost:host packageName:kPackageName serviceName:kServiceName]);
}

// Override superclass initializer to disallow different package and service names.
- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName {
  return [self initWithHost:host];
}


#pragma mark EmptyCall(Empty) returns (Empty)

- (void)emptyCallWithRequest:(RMTEmpty *)request handler:(void(^)(RMTEmpty *response, NSError *error))handler{
  [[self RPCToEmptyCallWithRequest:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToEmptyCallWithRequest:(RMTEmpty *)request handler:(void(^)(RMTEmpty *response, NSError *error))handler{
  return [self RPCToMethod:@"EmptyCall"
            requestsWriter:[GRXWriter writerWithValue:request]
             responseClass:[RMTEmpty class]
        responsesWriteable:[GRXWriteable writeableWithSingleValueHandler:handler]];
}
#pragma mark UnaryCall(SimpleRequest) returns (SimpleResponse)

- (void)unaryCallWithRequest:(RMTSimpleRequest *)request handler:(void(^)(RMTSimpleResponse *response, NSError *error))handler{
  [[self RPCToUnaryCallWithRequest:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToUnaryCallWithRequest:(RMTSimpleRequest *)request handler:(void(^)(RMTSimpleResponse *response, NSError *error))handler{
  return [self RPCToMethod:@"UnaryCall"
            requestsWriter:[GRXWriter writerWithValue:request]
             responseClass:[RMTSimpleResponse class]
        responsesWriteable:[GRXWriteable writeableWithSingleValueHandler:handler]];
}
#pragma mark StreamingOutputCall(StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)streamingOutputCallWithRequest:(RMTStreamingOutputCallRequest *)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  [[self RPCToStreamingOutputCallWithRequest:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToStreamingOutputCallWithRequest:(RMTStreamingOutputCallRequest *)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  return [self RPCToMethod:@"StreamingOutputCall"
            requestsWriter:[GRXWriter writerWithValue:request]
             responseClass:[RMTStreamingOutputCallResponse class]
        responsesWriteable:[GRXWriteable writeableWithStreamHandler:handler]];
}
#pragma mark StreamingInputCall(stream StreamingInputCallRequest) returns (StreamingInputCallResponse)

- (void)streamingInputCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RMTStreamingInputCallResponse *response, NSError *error))handler{
  [[self RPCToStreamingInputCallWithRequestsWriter:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToStreamingInputCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RMTStreamingInputCallResponse *response, NSError *error))handler{
  return [self RPCToMethod:@"StreamingInputCall"
            requestsWriter:request
             responseClass:[RMTStreamingInputCallResponse class]
        responsesWriteable:[GRXWriteable writeableWithSingleValueHandler:handler]];
}
#pragma mark FullDuplexCall(stream StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)fullDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  [[self RPCToFullDuplexCallWithRequestsWriter:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToFullDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  return [self RPCToMethod:@"FullDuplexCall"
            requestsWriter:request
             responseClass:[RMTStreamingOutputCallResponse class]
        responsesWriteable:[GRXWriteable writeableWithStreamHandler:handler]];
}
#pragma mark HalfDuplexCall(stream StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)halfDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  [[self RPCToHalfDuplexCallWithRequestsWriter:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToHalfDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler{
  return [self RPCToMethod:@"HalfDuplexCall"
            requestsWriter:request
             responseClass:[RMTStreamingOutputCallResponse class]
        responsesWriteable:[GRXWriteable writeableWithStreamHandler:handler]];
}
@end
