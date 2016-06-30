/*
 *
 * Copyright 2016, Google Inc.
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

#import <GRPCClient/GRPCCall.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <RxLibrary/GRXWriter+Transformations.h>

@interface ViewController : UIViewController
@end

@implementation ViewController
- (void)viewDidLoad {
  [super viewDidLoad];

  NSString *host = @"grpc-test.sandbox.googleapis.com";

  GRPCProtoMethod *method = [[GRPCProtoMethod alloc] initWithPackage:@"grpc.testing"
                                                             service:@"TestService"
                                                              method:@"StreamingOutputCall"];

  __block void (^startCall)() = ^{
    GRXWriter *loggingRequestWriter = [[GRXWriter writerWithValue:[NSData data]] map:^id(id value) {
      NSLog(@"Sending request.");
      return value;
    }];

    GRPCCall *call = [[GRPCCall alloc] initWithHost:host
                                               path:method.HTTPPath
                                     requestsWriter:loggingRequestWriter];

    [call startWithWriteable:[GRXWriteable writeableWithEventHandler:^(BOOL done, id value,
                                                                       NSError *error) {
      if (!done) {
        return;
      }
      if (error) {
        NSLog(@"Finished with error %@", error);
      } else {
        NSLog(@"Finished successfully.");
      }

      dispatch_time_t oneSecond = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC));
      dispatch_after(oneSecond, dispatch_get_main_queue(), startCall);
    }]];
  };

  startCall();
}
@end
