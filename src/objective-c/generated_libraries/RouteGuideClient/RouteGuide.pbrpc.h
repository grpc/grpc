#import "RouteGuide.pbobjc.h"
#import <gRPC/ProtoService.h>


@protocol GRXWriteable;
@protocol GRXWriter;

@protocol RGDRouteGuide <NSObject>

#pragma mark GetFeature(Point) returns (Feature)

- (void)getFeatureWithRequest:(RGDPoint *)request handler:(void(^)(RGDFeature *response, NSError *error))handler;

- (ProtoRPC *)RPCToGetFeatureWithRequest:(RGDPoint *)request handler:(void(^)(RGDFeature *response, NSError *error))handler;


#pragma mark ListFeatures(Rectangle) returns (stream Feature)

- (void)listFeaturesWithRequest:(RGDRectangle *)request handler:(void(^)(BOOL done, RGDFeature *response, NSError *error))handler;

- (ProtoRPC *)RPCToListFeaturesWithRequest:(RGDRectangle *)request handler:(void(^)(BOOL done, RGDFeature *response, NSError *error))handler;


#pragma mark RecordRoute(stream Point) returns (RouteSummary)

- (void)recordRouteWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RGDRouteSummary *response, NSError *error))handler;

- (ProtoRPC *)RPCToRecordRouteWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(RGDRouteSummary *response, NSError *error))handler;


#pragma mark RouteChat(stream RouteNote) returns (stream RouteNote)

- (void)routeChatWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RGDRouteNote *response, NSError *error))handler;

- (ProtoRPC *)RPCToRouteChatWithRequestsWriter:(id<GRXWriter>)request handler:(void(^)(BOOL done, RGDRouteNote *response, NSError *error))handler;


@end

// Basic service implementation, over gRPC, that only does marshalling and parsing.
@interface RGDRouteGuide : ProtoService<RGDRouteGuide>
- (instancetype)initWithHost:(NSString *)host NS_DESIGNATED_INITIALIZER;
@end
