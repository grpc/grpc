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

#pragma mark Get Feature

@interface GetFeatureViewController : UIViewController
@end

@implementation GetFeatureViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  RTGRouteGuide *client = [[RTGRouteGuide alloc] initWithHost:kHostAddress];

  RTGPoint *point = [RTGPoint message];
  point.latitude = 409146138;
  point.longitude = -746188906;

  [client getFeatureWithRequest:point handler:^(RTGFeature *response, NSError *error) {
    NSLog(@"Response: %@", response);
  }];
}

@end

#pragma mark List Features

@interface ListFeaturesViewController : UIViewController
@end

@implementation ListFeaturesViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  // Do any additional setup after loading the view, typically from a nib.
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
