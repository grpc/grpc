#import "RouteGuide.pbrpc.h"
#import <gRPC/GRXWriteable.h>
#import <gRPC/GRXWriter+Immediate.h>
#import <gRPC/ProtoRPC.h>

static NSString *const kPackageName = @"grpc.example.routeguide";
static NSString *const kServiceName = @"RouteGuide";

@implementation RGDRouteGuide

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


#pragma mark GetFeature(Point) returns (Feature)

- (void)getFeatureWithRequest:(RGDPoint *)request handler:(void(^)(RGDFeature *response, NSError *error))handler{
  [[self RPCToGetFeatureWithRequest:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToGetFeatureWithRequest:(RGDPoint *)request handler:(void(^)(RGDFeature *response, NSError *error))handler{
  return [self RPCToMethod:@"GetFeature"
            requestsWriter:[GRXWriter writerWithValue:request]
             responseClass:[RGDFeature class]
        responsesWriteable:[GRXWriteable writeableWithSingleValueHandler:handler]];
}
#pragma mark ListFeatures(Rectangle) returns (stream Feature)

- (void)listFeaturesWithRequest:(RGDRectangle *)request handler:(void(^)(BOOL done, RGDFeature *response, NSError *error))handler{
  [[self RPCToListFeaturesWithRequest:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToListFeaturesWithRequest:(RGDRectangle *)request handler:(void(^)(BOOL done, RGDFeature *response, NSError *error))handler{
  return [self RPCToMethod:@"ListFeatures"
            requestsWriter:[GRXWriter writerWithValue:request]
             responseClass:[RGDFeature class]
        responsesWriteable:[GRXWriteable writeableWithStreamHandler:handler]];
}
#pragma mark RecordRoute(stream Point) returns (RouteSummary)

- (void)recordRouteWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RGDRouteSummary *response, NSError *error))handler{
  [[self RPCToRecordRouteWithRequestsWriter:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToRecordRouteWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RGDRouteSummary *response, NSError *error))handler{
  return [self RPCToMethod:@"RecordRoute"
            requestsWriter:request
             responseClass:[RGDRouteSummary class]
        responsesWriteable:[GRXWriteable writeableWithSingleValueHandler:handler]];
}
#pragma mark RouteChat(stream RouteNote) returns (stream RouteNote)

- (void)routeChatWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RGDRouteNote *response, NSError *error))handler{
  [[self RPCToRouteChatWithRequestsWriter:request handler:handler] start];
}
// Returns a not-yet-started RPC object.
- (ProtoRPC *)RPCToRouteChatWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RGDRouteNote *response, NSError *error))handler{
  return [self RPCToMethod:@"RouteChat"
            requestsWriter:request
             responseClass:[RGDRouteNote class]
        responsesWriteable:[GRXWriteable writeableWithStreamHandler:handler]];
}
@end
