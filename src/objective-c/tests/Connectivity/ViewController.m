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
