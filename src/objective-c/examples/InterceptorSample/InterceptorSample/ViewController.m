/*
 *
 * Copyright 2019 gRPC authors.
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
#if USE_FRAMEWORKS
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#else
#import "src/objective-c/examples/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/examples/RemoteTestClient/Test.pbrpc.h"
#endif

#import "CacheInterceptor.h"

static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";

@interface ViewController () <GRPCResponseHandler>

@end

@implementation ViewController {
  GRPCCallOptions *_options;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];

  id<GRPCInterceptorFactory> factory = [[CacheContext alloc] init];
  options.interceptorFactories = @[ factory ];
  _options = options;
}

- (IBAction)tapCall:(id)sender {
  GRPCProtoMethod *kUnaryCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                                       service:kService
                                                                        method:@"UnaryCall"];

  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:@"grpc-test.sandbox.googleapis.com"
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyCacheableRequest];

  GRPCCall2 *call = [[GRPCCall2 alloc] initWithRequestOptions:requestOptions
                                              responseHandler:self
                                                  callOptions:_options];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 100;

  [call start];
  [call writeData:[request data]];
  [call finish];
}

- (dispatch_queue_t)dispatchQueue {
  return dispatch_get_main_queue();
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  NSLog(@"Header: %@", initialMetadata);
}

- (void)didReceiveData:(id)data {
  NSLog(@"Message: %@", data);
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  NSLog(@"Trailer: %@\nError: %@", trailingMetadata, error);
}

@end
