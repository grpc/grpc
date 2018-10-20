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

#define TEST_TIMEOUT 32

NSString *kDummyHost = @"dummy.host";

@interface ChannelPoolTest : XCTestCase

@end

@implementation ChannelPoolTest

+ (void)setUp {
  grpc_init();
}

- (void)testCreateChannel {
  NSString *kDummyHost = @"dummy.host";
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeInsecure;
  GRPCCallOptions *options2 = [options1 copy];
  GRPCChannelConfiguration *config1 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options1];
  GRPCChannelConfiguration *config2 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options2];
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];

  GRPCChannel *channel1 = [pool channelWithConfiguration:config1];
  GRPCChannel *channel2 = [pool channelWithConfiguration:config2];
  XCTAssertEqual(channel1, channel2);
}

- (void)testChannelRemove {
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeInsecure;
  GRPCChannelConfiguration *config1 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options1];
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCChannel *channel1 = [pool channelWithConfiguration:config1];
  [pool removeChannel:channel1];
  GRPCChannel *channel2 = [pool channelWithConfiguration:config1];
  XCTAssertNotEqual(channel1, channel2);
}

extern NSTimeInterval kChannelDestroyDelay;

- (void)testChannelTimeoutCancel {
  NSTimeInterval kOriginalInterval = kChannelDestroyDelay;
  kChannelDestroyDelay = 3.0;
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeInsecure;
  GRPCChannelConfiguration *config1 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options1];
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCChannel *channel1 = [pool channelWithConfiguration:config1];
  [channel1 unref];
  sleep(1);
  GRPCChannel *channel2 = [pool channelWithConfiguration:config1];
  XCTAssertEqual(channel1, channel2);
  sleep((int)kChannelDestroyDelay + 2);
  GRPCChannel *channel3 = [pool channelWithConfiguration:config1];
  XCTAssertEqual(channel1, channel3);
  kChannelDestroyDelay = kOriginalInterval;
}

- (void)testChannelDisconnect {
  NSString *kDummyHost = @"dummy.host";
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeInsecure;
  GRPCCallOptions *options2 = [options1 copy];
  GRPCChannelConfiguration *config1 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options1];
  GRPCChannelConfiguration *config2 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options2];
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];

  GRPCChannel *channel1 = [pool channelWithConfiguration:config1];
  [pool removeAndCloseAllChannels];
  GRPCChannel *channel2 = [pool channelWithConfiguration:config2];
  XCTAssertNotEqual(channel1, channel2);
}

- (void)testClearChannels {
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeInsecure;
  GRPCMutableCallOptions *options2 = [[GRPCMutableCallOptions alloc] init];
  options2.transportType = GRPCTransportTypeChttp2BoringSSL;
  GRPCChannelConfiguration *config1 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options1];
  GRPCChannelConfiguration *config2 =
      [[GRPCChannelConfiguration alloc] initWithHost:kDummyHost callOptions:options2];
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];

  GRPCChannel *channel1 = [pool channelWithConfiguration:config1];
  GRPCChannel *channel2 = [pool channelWithConfiguration:config2];
  XCTAssertNotEqual(channel1, channel2);

  [pool removeAndCloseAllChannels];
  GRPCChannel *channel3 = [pool channelWithConfiguration:config1];
  GRPCChannel *channel4 = [pool channelWithConfiguration:config2];
  XCTAssertNotEqual(channel1, channel3);
  XCTAssertNotEqual(channel2, channel4);
}

@end
