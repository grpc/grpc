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

#import "../../GRPCClient/private/GRPCCore/GRPCChannel.h"
#import "../../GRPCClient/private/GRPCCore/GRPCChannelPool+Test.h"
#import "../../GRPCClient/private/GRPCCore/GRPCCompletionQueue.h"

#define TEST_TIMEOUT 32

static NSString *kPhonyHost = @"phony.host";
static NSString *kPhonyHost2 = @"phony.host.2";
static NSString *kPhonyPath = @"/phony/path";

@interface ChannelPoolTest : XCTestCase

@end

@implementation ChannelPoolTest

+ (void)setUp {
  grpc_init();
}

- (void)testCreateAndCacheChannel {
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] initTestPool];
  GRPCCallOptions *options1 = [[GRPCCallOptions alloc] init];
  GRPCCallOptions *options2 = [options1 copy];
  GRPCMutableCallOptions *options3 = [options1 mutableCopy];
  options3.transportType = GRPCTransportTypeInsecure;

  GRPCPooledChannel *channel1 = [pool channelWithHost:kPhonyHost callOptions:options1];
  GRPCPooledChannel *channel2 = [pool channelWithHost:kPhonyHost callOptions:options2];
  GRPCPooledChannel *channel3 = [pool channelWithHost:kPhonyHost callOptions:options3];
  GRPCPooledChannel *channel4 = [pool channelWithHost:kPhonyHost2 callOptions:options1];

  XCTAssertNotNil(channel1);
  XCTAssertNotNil(channel2);
  XCTAssertNotNil(channel3);
  XCTAssertNotNil(channel4);
  XCTAssertEqual(channel1, channel2);
  XCTAssertNotEqual(channel1, channel3);
  XCTAssertNotEqual(channel1, channel4);
  XCTAssertNotEqual(channel3, channel4);
}

@end
