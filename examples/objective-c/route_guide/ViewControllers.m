/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

@property (weak, nonatomic) IBOutlet UILabel *outputLabel;

@end

@implementation GetFeatureViewController {
  RTGRouteGuide *_service;
}

- (void)execRequest {
  void (^handler)(RTGFeature *response, NSError *error) = ^(RTGFeature *response, NSError *error) {
    // TODO(makdharma): Remove boilerplate by consolidating into one log function.
    if (response.name.length) {
      NSString *str =[NSString stringWithFormat:@"%@\nFound feature called %@ at %@.", self.outputLabel.text, response.location, response.name];
      self.outputLabel.text = str;
      NSLog(@"Found feature called %@ at %@.", response.name, response.location);
    } else if (response) {
      NSString *str =[NSString stringWithFormat:@"%@\nFound no features at %@",  self.outputLabel.text,response.location];
      self.outputLabel.text = str;
      NSLog(@"Found no features at %@", response.location);
    } else {
      NSString *str =[NSString stringWithFormat:@"%@\nRPC error: %@", self.outputLabel.text, error];
      self.outputLabel.text = str;
      NSLog(@"RPC error: %@", error);
    }
  };

  RTGPoint *point = [RTGPoint message];
  point.latitude = 409146138;
  point.longitude = -746188906;

  [_service getFeatureWithRequest:point handler:handler];
  [_service getFeatureWithRequest:[RTGPoint message] handler:handler];
}

- (void)viewDidLoad {
  [super viewDidLoad];

  // This only needs to be done once per host, before creating service objects for that host.
  [GRPCCall useInsecureConnectionsForHost:kHostAddress];

  _service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];
}

- (void)viewDidAppear:(BOOL)animated {
  self.outputLabel.text = @"RPC log:";
  self.outputLabel.numberOfLines = 0;
  self.outputLabel.font = [UIFont fontWithName:@"Helvetica Neue" size:8.0];
  [self execRequest];
}

@end


#pragma mark Demo: List Features

/**
 * Run the listFeatures demo. Calls listFeatures with a rectangle containing all of the features in
 * the pre-generated database. Prints each response as it comes in.
 */
@interface ListFeaturesViewController : UIViewController

@property (weak, nonatomic) IBOutlet UILabel *outputLabel;

@end

@implementation ListFeaturesViewController {
  RTGRouteGuide *_service;
}

- (void)execRequest {
  RTGRectangle *rectangle = [RTGRectangle message];
  rectangle.lo.latitude = 405E6;
  rectangle.lo.longitude = -750E6;
  rectangle.hi.latitude = 410E6;
  rectangle.hi.longitude = -745E6;

  NSLog(@"Looking for features between %@ and %@", rectangle.lo, rectangle.hi);
  [_service listFeaturesWithRequest:rectangle
                      eventHandler:^(BOOL done, RTGFeature *response, NSError *error) {
    if (response) {
      NSString *str =[NSString stringWithFormat:@"%@\nFound feature at %@ called %@.", self.outputLabel.text, response.location, response.name];
      self.outputLabel.text = str;
      NSLog(@"Found feature at %@ called %@.", response.location, response.name);
    } else if (error) {
      NSString *str =[NSString stringWithFormat:@"%@\nRPC error: %@", self.outputLabel.text, error];
      self.outputLabel.text = str;
      NSLog(@"RPC error: %@", error);
    }
  }];
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];
}

- (void)viewDidAppear:(BOOL)animated {
  self.outputLabel.text = @"RPC log:";
  self.outputLabel.numberOfLines = 0;
  self.outputLabel.font = [UIFont fontWithName:@"Helvetica Neue" size:8.0];
  [self execRequest];
}

@end


#pragma mark Demo: Record Route

/**
 * Run the recordRoute demo. Sends several randomly chosen points from the pre-generated feature
 * database with a variable delay in between. Prints the statistics when they are sent from the
 * server.
 */
@interface RecordRouteViewController : UIViewController

@property (weak, nonatomic) IBOutlet UILabel *outputLabel;

@end

@implementation RecordRouteViewController {
  RTGRouteGuide *_service;
}

- (void)execRequest {
  NSString *dataBasePath = [NSBundle.mainBundle pathForResource:@"route_guide_db"
                                                         ofType:@"json"];
  NSData *dataBaseContent = [NSData dataWithContentsOfFile:dataBasePath];
  NSArray *features = [NSJSONSerialization JSONObjectWithData:dataBaseContent options:0 error:NULL];

  GRXWriter *locations = [[GRXWriter writerWithContainer:features] map:^id(id feature) {
    RTGPoint *location = [RTGPoint message];
    location.longitude = [((NSNumber *) feature[@"location"][@"longitude"]) intValue];
    location.latitude = [((NSNumber *) feature[@"location"][@"latitude"]) intValue];
    NSString *str =[NSString stringWithFormat:@"%@\nVisiting point %@", self.outputLabel.text, location];
    self.outputLabel.text = str;
    NSLog(@"Visiting point %@", location);
    return location;
  }];

  [_service recordRouteWithRequestsWriter:locations
                                 handler:^(RTGRouteSummary *response, NSError *error) {
    if (response) {
      NSString *str =[NSString stringWithFormat:
                      @"%@\nFinished trip with %i points\nPassed %i features\n"
                      "Travelled %i meters\nIt took %i seconds",
                      self.outputLabel.text, response.pointCount, response.featureCount,
                      response.distance, response.elapsedTime];
      self.outputLabel.text = str;
      NSLog(@"Finished trip with %i points", response.pointCount);
      NSLog(@"Passed %i features", response.featureCount);
      NSLog(@"Travelled %i meters", response.distance);
      NSLog(@"It took %i seconds", response.elapsedTime);
    } else {
      NSString *str =[NSString stringWithFormat:@"%@\nRPC error: %@", self.outputLabel.text, error];
      self.outputLabel.text = str;
      NSLog(@"RPC error: %@", error);
    }
  }];
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];
}

- (void)viewDidAppear:(BOOL)animated {
  self.outputLabel.text = @"RPC log:";
  self.outputLabel.numberOfLines = 0;
  self.outputLabel.font = [UIFont fontWithName:@"Helvetica Neue" size:8.0];
  [self execRequest];
}

@end


#pragma mark Demo: Route Chat

/**
 * Run the routeChat demo. Send some chat messages, and print any chat messages that are sent from
 * the server.
 */
@interface RouteChatViewController : UIViewController

@property (weak, nonatomic) IBOutlet UILabel *outputLabel;

@end

@implementation RouteChatViewController {
  RTGRouteGuide *_service;
}

- (void)execRequest {
  NSArray *notes = @[[RTGRouteNote noteWithMessage:@"First message" latitude:0 longitude:0],
                     [RTGRouteNote noteWithMessage:@"Second message" latitude:0 longitude:1],
                     [RTGRouteNote noteWithMessage:@"Third message" latitude:1 longitude:0],
                     [RTGRouteNote noteWithMessage:@"Fourth message" latitude:0 longitude:0]];
  GRXWriter *notesWriter = [[GRXWriter writerWithContainer:notes] map:^id(RTGRouteNote *note) {
    NSLog(@"Sending message %@ at %@", note.message, note.location);
    return note;
  }];

  [_service routeChatWithRequestsWriter:notesWriter
                          eventHandler:^(BOOL done, RTGRouteNote *note, NSError *error) {
    if (note) {
      NSString *str =[NSString stringWithFormat:@"%@\nGot message %@ at %@",
                      self.outputLabel.text, note.message, note.location];
      self.outputLabel.text = str;
      NSLog(@"Got message %@ at %@", note.message, note.location);
    } else if (error) {
      NSString *str =[NSString stringWithFormat:@"%@\nRPC error: %@", self.outputLabel.text, error];
      self.outputLabel.text = str;
      NSLog(@"RPC error: %@", error);
    }
    if (done) {
      NSLog(@"Chat ended.");
    }
  }];
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _service = [[RTGRouteGuide alloc] initWithHost:kHostAddress];
}

- (void)viewDidAppear:(BOOL)animated {
  // TODO(makarandd): Set these properties through UI builder
  self.outputLabel.text = @"RPC log:";
  self.outputLabel.numberOfLines = 0;
  self.outputLabel.font = [UIFont fontWithName:@"Helvetica Neue" size:8.0];
  [self execRequest];
}

@end
