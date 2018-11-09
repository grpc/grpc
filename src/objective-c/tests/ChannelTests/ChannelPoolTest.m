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

#import "../../GRPCClient/private/GRPCChannel.h"
#import "../../GRPCClient/private/GRPCChannelPool.h"
#import "../../GRPCClient/private/GRPCCompletionQueue.h"

#define TEST_TIMEOUT 32

NSString *kDummyHost = @"dummy.host";

@interface ChannelPoolTest : XCTestCase

@end

@implementation ChannelPoolTest

+ (void)setUp {
  grpc_init();
}

- (void)testChannelPooling {
  NSString *kDummyHost = @"dummy.host";
  NSString *kDummyHost2 = @"dummy.host2";

  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  GRPCCallOptions *options2 = [options1 copy];
  GRPCMutableCallOptions *options3 = [options2 mutableCopy];
  options3.transportType = GRPCTransportTypeInsecure;

  GRPCChannelPool *pool = [GRPCChannelPool sharedInstance];

  GRPCChannel *channel1 = [pool channelWithHost:kDummyHost
                                    callOptions:options1];
  GRPCChannel *channel2 = [pool channelWithHost:kDummyHost
                                    callOptions:options2];
  GRPCChannel *channel3 = [pool channelWithHost:kDummyHost2
                                    callOptions:options1];
  GRPCChannel *channel4 = [pool channelWithHost:kDummyHost
                                    callOptions:options3];
  XCTAssertEqual(channel1, channel2);
  XCTAssertNotEqual(channel1, channel3);
  XCTAssertNotEqual(channel1, channel4);
  XCTAssertNotEqual(channel3, channel4);
}

- (void)testDestroyAllChannels {
  NSString *kDummyHost = @"dummy.host";

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  GRPCChannelPool *pool = [GRPCChannelPool sharedInstance];
  GRPCChannel *channel = [pool channelWithHost:kDummyHost
                                   callOptions:options];
  grpc_call *call = [channel unmanagedCallWithPath:@"dummy.path"
                                   completionQueue:[GRPCCompletionQueue completionQueue]
                                       callOptions:options
                                      disconnected:nil];
  [pool destroyAllChannels];
  XCTAssertTrue(channel.disconnected);
  GRPCChannel *channel2 = [pool channelWithHost:kDummyHost
                                    callOptions:options];
  XCTAssertNotEqual(channel, channel2);
  grpc_call_unref(call);
}

- (void)testGetChannelBeforeChannelTimedDisconnection {
  NSString *kDummyHost = @"dummy.host";
  const NSTimeInterval kDestroyDelay = 1;

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  GRPCChannelPool *pool = [GRPCChannelPool sharedInstance];
  GRPCChannel *channel = [pool channelWithHost:kDummyHost
                                   callOptions:options
                                  destroyDelay:kDestroyDelay];
  grpc_call *call = [channel unmanagedCallWithPath:@"dummy.path"
                                   completionQueue:[GRPCCompletionQueue completionQueue]
                                       callOptions:options
                                      disconnected:nil];
  grpc_call_unref(call);
  [channel unref];

  // Test that we can still get the channel at this time
  GRPCChannel *channel2 = [pool channelWithHost:kDummyHost
                                    callOptions:options
                                   destroyDelay:kDestroyDelay];
  XCTAssertEqual(channel, channel2);
  call = [channel2 unmanagedCallWithPath:@"dummy.path"
                         completionQueue:[GRPCCompletionQueue completionQueue]
                             callOptions:options
                            disconnected:nil];

  // Test that after the destroy delay, the channel is still alive
  sleep(kDestroyDelay + 1);
  XCTAssertFalse(channel.disconnected);
}

- (void)testGetChannelAfterChannelTimedDisconnection {
  NSString *kDummyHost = @"dummy.host";
  const NSTimeInterval kDestroyDelay = 1;

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  GRPCChannelPool *pool = [GRPCChannelPool sharedInstance];
  GRPCChannel *channel = [pool channelWithHost:kDummyHost
                                   callOptions:options
                                  destroyDelay:kDestroyDelay];
  grpc_call *call = [channel unmanagedCallWithPath:@"dummy.path"
                                   completionQueue:[GRPCCompletionQueue completionQueue]
                                       callOptions:options
                                      disconnected:nil];
  grpc_call_unref(call);
  [channel unref];

  sleep(kDestroyDelay + 1);

  // Test that we get new channel to the same host and with the same callOptions
  GRPCChannel *channel2 = [pool channelWithHost:kDummyHost
                                    callOptions:options
                                   destroyDelay:kDestroyDelay];
  XCTAssertNotEqual(channel, channel2);
}

@end
