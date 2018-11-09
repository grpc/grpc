/*
 *
 * Copyright 2018 gRPC authors.
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

#import <XCTest/XCTest.h>

#import "../../GRPCClient/GRPCCallOptions.h"
#import "../../GRPCClient/private/GRPCChannel.h"
#import "../../GRPCClient/private/GRPCCompletionQueue.h"

@interface ChannelTests : XCTestCase

@end

@implementation ChannelTests

+ (void)setUp {
  grpc_init();
}

- (void)testTimedDisconnection {
  NSString * const kHost = @"grpc-test.sandbox.googleapis.com";
  const NSTimeInterval kDestroyDelay = 1;
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCChannelConfiguration *configuration = [[GRPCChannelConfiguration alloc] initWithHost:kHost callOptions:options];
  GRPCChannel *channel = [[GRPCChannel alloc] initWithChannelConfiguration:configuration
                                                              destroyDelay:kDestroyDelay];
  BOOL disconnected;
  grpc_call *call = [channel unmanagedCallWithPath:@"dummy.path"
                                   completionQueue:[GRPCCompletionQueue completionQueue]
                                       callOptions:options
                                      disconnected:&disconnected];
  XCTAssertFalse(disconnected);
  grpc_call_unref(call);
  [channel unref];
  XCTAssertFalse(channel.disconnected, @"Channel is pre-maturely disconnected.");
  sleep(kDestroyDelay + 1);
  XCTAssertTrue(channel.disconnected, @"Channel is not disconnected after delay.");

  // Check another call creation returns null and indicates disconnected.
  call = [channel unmanagedCallWithPath:@"dummy.path"
                        completionQueue:[GRPCCompletionQueue completionQueue]
                            callOptions:options
                           disconnected:&disconnected];
  XCTAssert(call == NULL);
  XCTAssertTrue(disconnected);
}

- (void)testForceDisconnection {
  NSString * const kHost = @"grpc-test.sandbox.googleapis.com";
  const NSTimeInterval kDestroyDelay = 1;
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCChannelConfiguration *configuration = [[GRPCChannelConfiguration alloc] initWithHost:kHost callOptions:options];
  GRPCChannel *channel = [[GRPCChannel alloc] initWithChannelConfiguration:configuration
                                                              destroyDelay:kDestroyDelay];
  grpc_call *call = [channel unmanagedCallWithPath:@"dummy.path"
                                   completionQueue:[GRPCCompletionQueue completionQueue]
                                       callOptions:options
                                      disconnected:nil];
  grpc_call_unref(call);
  [channel disconnect];
  XCTAssertTrue(channel.disconnected, @"Channel is not disconnected.");

  // Test calling another unref here will not crash
  [channel unref];
}

@end
