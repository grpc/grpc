//
//  InterfaceController.m
//  watchOS-sample WatchKit Extension
//
//  Created by Tony Lu on 7/12/19.
//  Copyright © 2019 Tony Lu. All rights reserved.
//

#import "InterfaceController.h"

#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>

@interface InterfaceController ()<GRPCProtoResponseHandler>

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

- (void)willActivate {
  // This method is called when watch view controller is about to be visible to user
  [super willActivate];
}

- (void)didDeactivate {
  // This method is called when watch view controller is no longer visible
  [super didDeactivate];
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



