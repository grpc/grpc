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
#import "InterfaceController.h"

#if USE_FRAMEWORKS
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#else
#import "src/objective-c/examples/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/examples/RemoteTestClient/Test.pbrpc.h"
#endif
#import <GRPCClient/GRPCCallOptions.h>
#import <ProtoRPC/ProtoRPC.h>

@interface InterfaceController () <GRPCProtoResponseHandler>

@end

@implementation InterfaceController {
  GRPCCallOptions *_options;
  RMTTestService *_service;
}

- (void)awakeWithContext:(id)context {
  [super awakeWithContext:context];

  // Configure interface objects here.

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  _options = options;

  _service = [[RMTTestService alloc] initWithHost:@"grpc-test.sandbox.googleapis.com"
                                      callOptions:_options];
}

- (IBAction)makeCall {
  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 100;
  GRPCUnaryProtoCall *call = [_service unaryCallWithMessage:request
                                            responseHandler:self
                                                callOptions:nil];
  [call start];
}

- (void)didReceiveProtoMessage:(GPBMessage *)message {
  NSLog(@"%@", [message data]);
}

- (dispatch_queue_t)dispatchQueue {
  return dispatch_get_main_queue();
}

@end
