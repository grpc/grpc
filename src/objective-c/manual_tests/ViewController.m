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

#import <UIKit/UIKit.h>

#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCCallOptions.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbrpc.h"

NSString *const kRemoteHost = @"grpc-test.sandbox.googleapis.com";
const int32_t kMessageSize = 100;

@interface ViewController : UIViewController <GRPCProtoResponseHandler>
@property(strong, nonatomic) UILabel *callStatusLabel;
@property(strong, nonatomic) UILabel *callCountLabel;
@end

@implementation ViewController {
  RMTTestService *_service;
  dispatch_queue_t _dispatchQueue;
  GRPCStreamingProtoCall *_call;
  int _calls_completed;
}
- (instancetype)init {
  self = [super init];
  _calls_completed = 0;
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
  _callStatusLabel = (UILabel *)[self.view viewWithTag:1];
  _callCountLabel = (UILabel *)[self.view viewWithTag:2];
}

- (void)startUnaryCall {
  if (_service == nil) {
    _service = [RMTTestService serviceWithHost:kRemoteHost];
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_callStatusLabel.text = @"";
  });

  // Set up request proto message
  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseType = RMTPayloadType_Compressable;
  request.responseSize = kMessageSize;
  request.payload.body = [NSMutableData dataWithLength:kMessageSize];

  GRPCUnaryProtoCall *call = [_service unaryCallWithMessage:request
                                            responseHandler:self
                                                callOptions:nil];

  [call start];
}

- (IBAction)tapUnaryCall:(id)sender {
  NSLog(@"In tapUnaryCall");
  [self startUnaryCall];
}

- (IBAction)tap10UnaryCalls:(id)sender {
  NSLog(@"In tap10UnaryCalls");
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(void) {
    // Background thread
    for (int i = 0; i < 10; ++i) {
      [self startUnaryCall];
      [NSThread sleepForTimeInterval:0.5];
    }
  });
}

- (IBAction)resetCounter:(id)sender {
  _calls_completed = 0;
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_callCountLabel.text =
        [NSString stringWithFormat:@"Calls completed: %d", self->_calls_completed];
    self->_callStatusLabel.text = @"";
  });
}

- (IBAction)tapStreamingCallStart:(id)sender {
  NSLog(@"In tapStreamingCallStart");
  if (_service == nil) {
    _service = [RMTTestService serviceWithHost:kRemoteHost];
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_callStatusLabel.text = @"";
  });

  // Set up request proto message
  RMTStreamingOutputCallRequest *request = RMTStreamingOutputCallRequest.message;
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = kMessageSize;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:kMessageSize];

  GRPCStreamingProtoCall *call = [_service fullDuplexCallWithResponseHandler:self callOptions:nil];
  [call start];
  _call = call;
  // display something to confirm the tester the call is started
}

- (IBAction)tapStreamingCallSend:(id)sender {
  NSLog(@"In tapStreamingCallSend");
  if (_call == nil) return;

  RMTStreamingOutputCallRequest *request = RMTStreamingOutputCallRequest.message;
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = kMessageSize;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:kMessageSize];

  [_call writeMessage:request];
}

- (IBAction)tapStreamingCallStop:(id)sender {
  NSLog(@"In tapStreamingCallStop");
  if (_call == nil) return;

  [_call finish];
  _call = nil;
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  NSLog(@"Recv initial metadata: %@", initialMetadata);
}

- (void)didReceiveProtoMessage:(GPBMessage *)message {
  NSLog(@"Recv message: %@", message);
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error {
  NSLog(@"Recv trailing metadata: %@, error: %@", trailingMetadata, error);
  if (error == nil) {
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_callStatusLabel.text = @"Call done";
    });
  } else {
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_callStatusLabel.text = @"Call failed";
    });
  }
  ++_calls_completed;
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_callCountLabel.text =
        [NSString stringWithFormat:@"Calls completed: %d", self->_calls_completed];
  });
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end
