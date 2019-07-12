//
//  ViewController.m
//  tvOS-sample
//
//  Created by Tony Lu on 7/12/19.
//  Copyright © 2019 Tony Lu. All rights reserved.
//

#import "ViewController.h"

#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>


static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";

@interface ViewController ()<GRPCProtoResponseHandler>

@end

@implementation ViewController {
  GRPCCallOptions *_options;
  RMTTestService *_service;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  _options = options;
  
  _service = [[RMTTestService alloc] initWithHost:@"grpc-test.sandbox.googleapis.com"
                                      callOptions:_options];
}

- (IBAction)makeCall:(id)sender {
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
