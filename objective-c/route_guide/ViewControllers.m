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
#import <RouteGuide/RouteGuide.pbrpc.h>

static NSString * const kHostAddress = @"http://localhost:50051";

// Category to override RTGPoint's description.
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

#pragma mark Get Feature

// Run the getFeature demo. Calls getFeature with a point known to have a feature and a point known
// not to have a feature.

@interface GetFeatureViewController : UIViewController
@end

@implementation GetFeatureViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  RTGRouteGuide *client = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

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

  [client getFeatureWithRequest:point handler:handler];
  [client getFeatureWithRequest:[RTGPoint message] handler:handler];
}

@end


#pragma mark List Features

// Run the listFeatures demo. Calls listFeatures with a rectangle containing all of the features in
// the pre-generated database. Prints each response as it comes in.

@interface ListFeaturesViewController : UIViewController
@end

@implementation ListFeaturesViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  RTGRouteGuide *client = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  RTGRectangle *rectangle = [RTGRectangle message];
  rectangle.lo.latitude = 405E6;
  rectangle.lo.longitude = -750E6;
  rectangle.hi.latitude = 410E6;
  rectangle.hi.longitude = -745E6;

  NSLog(@"Looking for features between %@ and %@", rectangle.lo, rectangle.hi);
  [client listFeaturesWithRequest:rectangle handler:^(BOOL done, RTGFeature *response, NSError *error) {
    if (response) {
      NSLog(@"Found feature at %@ called %@.", response.location, response.name);
    } else if (error) {
      NSLog(@"RPC error: %@", error);
    }
  }];
}

@end


#pragma mark Record Route

@interface RecordRouteViewController : UIViewController
@end

@implementation RecordRouteViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  // Do any additional setup after loading the view.
}

@end


#pragma mark Route Chat

@interface RouteChatViewController : UIViewController
@end

@implementation RouteChatViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  // Do any additional setup after loading the view.
}

@end
