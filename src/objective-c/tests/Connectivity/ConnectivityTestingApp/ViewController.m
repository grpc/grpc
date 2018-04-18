/*
 *
 * Copyright 2016 gRPC authors.
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

#import <GRPCClient/GRPCCall.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <RxLibrary/GRXWriter+Transformations.h>

#import "src/objective-c/GRPCClient/private/GRPCConnectivityMonitor.h"

NSString *host = @"grpc-test.sandbox.googleapis.com";

@interface ViewController : UIViewController
@end

@implementation ViewController
- (void)viewDidLoad {
  [super viewDidLoad];

  [GRPCConnectivityMonitor registerObserver:self selector:@selector(reachabilityChanged:)];
}

- (void)reachabilityChanged:(NSNotification *)note {
  NSLog(@"Reachability changed\n");
}

- (IBAction)tapUnary:(id)sender {
  // Create a unary call

  // A trivial proto message to generate a response
  char bytes[] = {0x10, 0x05, 0x1A, 0x07, 0x12, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00};

  GRPCProtoMethod *method = [[GRPCProtoMethod alloc] initWithPackage:@"grpc.testing"
                                                             service:@"TestService"
                                                              method:@"UnaryCall"];
  GRXWriter *loggingRequestWriter = [[GRXWriter
      writerWithValue:[NSData dataWithBytes:bytes length:sizeof(bytes)]] map:^id(id value) {
    NSLog(@"Sending request.");
    return value;
  }];
  GRPCCall *call =
      [[GRPCCall alloc] initWithHost:host path:method.HTTPPath requestsWriter:loggingRequestWriter];

  [call startWithWriteable:[GRXWriteable
                               writeableWithEventHandler:^(BOOL done, id value, NSError *error) {
                                 if (!done) {
                                   return;
                                 }
                                 NSLog(@"Unary call finished with error: %@", error);
                               }]];
}

- (IBAction)tapStreaming:(id)sender {
  // Create a streaming call

  // A trivial proto message to generate a response
  char bytes[] = {0x12, 0x02, 0x08, 0x02, 0x1A, 0x04, 0x12, 0x02, 0x00, 0x00};

  GRPCProtoMethod *method = [[GRPCProtoMethod alloc] initWithPackage:@"grpc.testing"
                                                             service:@"TestService"
                                                              method:@"FullDuplexCall"];

  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  [requestsBuffer writeValue:[NSData dataWithBytes:bytes length:sizeof(bytes)]];

  GRPCCall *call =
      [[GRPCCall alloc] initWithHost:host path:method.HTTPPath requestsWriter:requestsBuffer];

  [call startWithWriteable:[GRXWriteable
                               writeableWithEventHandler:^(BOOL done, id value, NSError *error) {
                                 if (!done) {
                                   return;
                                 }
                                 NSLog(@"Streaming call finished with error: %@", error);
                               }]];
}

@end
