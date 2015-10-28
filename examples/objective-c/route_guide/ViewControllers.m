/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import <UIKit/UIKit.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <RouteGuide/RouteGuide.pbrpc.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <RxLibrary/GRXWriter+Transformations.h>

static NSString * const kHostAddress = @"localhost:50051";

/** Category to override RTGPoint's description. */
@interface RTGPoint (Description)
- (NSString *)description;
@end

@implementation RTGPoint (Description)
- (NSString *)description {
  NSString *verticalDirection = self.latitude >= 0 ? @"N" : @"S";
  NSString *horizontalDirection = self.longitude >= 0 ? @"E" : @"W";
  return [NSString stringWithFormat:@"%.02f%@ %.02f%@",
          abs(self.latitude) / 1E7f, verticalDirection,
          abs(self.longitude) / 1E7f, horizontalDirection];
}
@end

/** Category to give RTGRouteNote a convenience constructor. */
@interface RTGRouteNote (Constructors)
+ (instancetype)noteWithMessage:(NSString *)message
                       latitude:(float)latitude
                      longitude:(float)longitude;
@end

@implementation RTGRouteNote (Constructors)
+ (instancetype)noteWithMessage:(NSString *)message
                       latitude:(float)latitude
                      longitude:(float)longitude {
  RTGRouteNote *note = [self message];
  note.message = message;
  note.location.latitude = (int32_t) latitude * 1E7;
  note.location.longitude = (int32_t) longitude * 1E7;
  return note;
}
@end


#pragma mark Demo: Get Feature

/**
 * Run the getFeature demo. Calls getFeature with a point known to have a feature and a point known
 * not to have a feature.
 */
@interface GetFeatureViewController : UIViewController
@end

@implementation GetFeatureViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  // This only needs to be done once per host, before creating service objects for that host.
  [GRPCCall useInsecureConnectionsForHost:kHostAddress];

  RTGRouteGuide *service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  void (^handler)(RTGFeature *response, NSError *error) = ^(RTGFeature *response, NSError *error) {
    if (response.name.length) {
      NSLog(@"Found feature called %@ at %@.", response.name, response.location);
    } else if (response) {
      NSLog(@"Found no features at %@", response.location);
    } else {
      NSLog(@"RPC error: %@", error);
    }
  };

  RTGPoint *point = [RTGPoint message];
  point.latitude = 409146138;
  point.longitude = -746188906;

  [service getFeatureWithRequest:point handler:handler];
  [service getFeatureWithRequest:[RTGPoint message] handler:handler];
}

@end


#pragma mark Demo: List Features

/**
 * Run the listFeatures demo. Calls listFeatures with a rectangle containing all of the features in
 * the pre-generated database. Prints each response as it comes in.
 */
@interface ListFeaturesViewController : UIViewController
@end

@implementation ListFeaturesViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  RTGRouteGuide *service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  RTGRectangle *rectangle = [RTGRectangle message];
  rectangle.lo.latitude = 405E6;
  rectangle.lo.longitude = -750E6;
  rectangle.hi.latitude = 410E6;
  rectangle.hi.longitude = -745E6;

  NSLog(@"Looking for features between %@ and %@", rectangle.lo, rectangle.hi);
  [service listFeaturesWithRequest:rectangle
                      eventHandler:^(BOOL done, RTGFeature *response, NSError *error) {
    if (response) {
      NSLog(@"Found feature at %@ called %@.", response.location, response.name);
    } else if (error) {
      NSLog(@"RPC error: %@", error);
    }
  }];
}

@end


#pragma mark Demo: Record Route

/**
 * Run the recordRoute demo. Sends several randomly chosen points from the pre-generated feature
 * database with a variable delay in between. Prints the statistics when they are sent from the
 * server.
 */
@interface RecordRouteViewController : UIViewController
@end

@implementation RecordRouteViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  NSString *dataBasePath = [NSBundle.mainBundle pathForResource:@"route_guide_db"
                                                         ofType:@"json"];
  NSData *dataBaseContent = [NSData dataWithContentsOfFile:dataBasePath];
  NSArray *features = [NSJSONSerialization JSONObjectWithData:dataBaseContent options:0 error:NULL];

  GRXWriter *locations = [[GRXWriter writerWithContainer:features] map:^id(id feature) {
    RTGPoint *location = [RTGPoint message];
    location.longitude = [((NSNumber *) feature[@"location"][@"longitude"]) intValue];
    location.latitude = [((NSNumber *) feature[@"location"][@"latitude"]) intValue];
    NSLog(@"Visiting point %@", location);
    return location;
  }];

  RTGRouteGuide *service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  [service recordRouteWithRequestsWriter:locations
                                 handler:^(RTGRouteSummary *response, NSError *error) {
    if (response) {
      NSLog(@"Finished trip with %i points", response.pointCount);
      NSLog(@"Passed %i features", response.featureCount);
      NSLog(@"Travelled %i meters", response.distance);
      NSLog(@"It took %i seconds", response.elapsedTime);
    } else {
      NSLog(@"RPC error: %@", error);
    }
  }];
}

@end


#pragma mark Demo: Route Chat

/**
 * Run the routeChat demo. Send some chat messages, and print any chat messages that are sent from
 * the server.
 */
@interface RouteChatViewController : UIViewController
@end

@implementation RouteChatViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  NSArray *notes = @[[RTGRouteNote noteWithMessage:@"First message" latitude:0 longitude:0],
                     [RTGRouteNote noteWithMessage:@"Second message" latitude:0 longitude:1],
                     [RTGRouteNote noteWithMessage:@"Third message" latitude:1 longitude:0],
                     [RTGRouteNote noteWithMessage:@"Fourth message" latitude:0 longitude:0]];
  GRXWriter *notesWriter = [[GRXWriter writerWithContainer:notes] map:^id(RTGRouteNote *note) {
    NSLog(@"Sending message %@ at %@", note.message, note.location);
    return note;
  }];

  RTGRouteGuide *service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  [service routeChatWithRequestsWriter:notesWriter
                          eventHandler:^(BOOL done, RTGRouteNote *note, NSError *error) {
    if (note) {
      NSLog(@"Got message %@ at %@", note.message, note.location);
    } else if (error) {
      NSLog(@"RPC error: %@", error);
    }
    if (done) {
      NSLog(@"Chat ended.");
    }
  }];
}

@end
