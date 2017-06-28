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

#import "ViewController.h"

#import <GRPCClient/GRPCCall.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <RxLibrary/GRXWriteable.h>

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  NSString * const kRemoteHost = @"grpc-test.sandbox.googleapis.com";

  RMTSimpleRequest *request = [[RMTSimpleRequest alloc] init];
  request.responseSize = 10;
  request.fillUsername = YES;
  request.fillOauthScope = YES;

  // Example gRPC call using a generated proto client library:

  RMTTestService *service = [[RMTTestService alloc] initWithHost:kRemoteHost];
  [service unaryCallWithRequest:request handler:^(RMTSimpleResponse *response, NSError *error) {
    if (response) {
      NSLog(@"Finished successfully with response:\n%@", response);
    } else if (error) {
      NSLog(@"Finished with error: %@", error);
    }
  }];


  // Same example call using the generic gRPC client library:

  GRPCProtoMethod *method = [[GRPCProtoMethod alloc] initWithPackage:@"grpc.testing"
                                                             service:@"TestService"
                                                              method:@"UnaryCall"];

  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRemoteHost
                                             path:method.HTTPPath
                                   requestsWriter:requestsWriter];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    RMTSimpleResponse *response = [RMTSimpleResponse parseFromData:value error:NULL];
    NSLog(@"Received response:\n%@", response);
  } completionHandler:^(NSError *errorOrNil) {
    if (errorOrNil) {
      NSLog(@"Finished with error: %@", errorOrNil);
    } else {
      NSLog(@"Finished successfully.");
    }
  }];

  [call startWithWriteable:responsesWriteable];
}

@end
