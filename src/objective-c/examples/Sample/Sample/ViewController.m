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
