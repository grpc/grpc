#import "Test.pbobjc.h"
#import <gRPC/ProtoService.h>

#import "Empty.pbobjc.h"
#import "Messages.pbobjc.h"

@protocol GRXWriteable;
@protocol GRXWriter;

@protocol RMTTestService <NSObject>

#pragma mark EmptyCall(Empty) returns (Empty)

- (void)emptyCallWithRequest:(RMTEmpty *)request handler:(void(^)(RMTEmpty *response, NSError *error))handler;

- (ProtoRPC *)RPCToEmptyCallWithRequest:(RMTEmpty *)request handler:(void(^)(RMTEmpty *response, NSError *error))handler;


#pragma mark UnaryCall(SimpleRequest) returns (SimpleResponse)

- (void)unaryCallWithRequest:(RMTSimpleRequest *)request handler:(void(^)(RMTSimpleResponse *response, NSError *error))handler;

- (ProtoRPC *)RPCToUnaryCallWithRequest:(RMTSimpleRequest *)request handler:(void(^)(RMTSimpleResponse *response, NSError *error))handler;


#pragma mark StreamingOutputCall(StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)streamingOutputCallWithRequest:(RMTStreamingOutputCallRequest *)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;

- (ProtoRPC *)RPCToStreamingOutputCallWithRequest:(RMTStreamingOutputCallRequest *)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;


#pragma mark StreamingInputCall(stream StreamingInputCallRequest) returns (StreamingInputCallResponse)

- (void)streamingInputCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RMTStreamingInputCallResponse *response, NSError *error))handler;

- (ProtoRPC *)RPCToStreamingInputCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RMTStreamingInputCallResponse *response, NSError *error))handler;


#pragma mark FullDuplexCall(stream StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)fullDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;

- (ProtoRPC *)RPCToFullDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;


#pragma mark HalfDuplexCall(stream StreamingOutputCallRequest) returns (stream StreamingOutputCallResponse)

- (void)halfDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;

- (ProtoRPC *)RPCToHalfDuplexCallWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error))handler;


@end

// Basic service implementation, over gRPC, that only does marshalling and parsing.
@interface RMTTestService : ProtoService<RMTTestService>
- (instancetype)initWithHost:(NSString *)host NS_DESIGNATED_INITIALIZER;
@end
