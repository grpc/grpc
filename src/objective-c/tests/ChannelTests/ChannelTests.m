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

@interface ChannelTests : XCTestCase

@end

@implementation ChannelTests

+ (void)setUp {
  grpc_init();
}

- (void)testSameConfiguration {
  NSString *host = @"grpc-test.sandbox.googleapis.com";
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.userAgentPrefix = @"TestUAPrefix";
  NSMutableDictionary *args = [NSMutableDictionary new];
  args[@"abc"] = @"xyz";
  options.additionalChannelArgs = [args copy];
  GRPCChannel *channel1 = [GRPCChannel channelWithHost:host callOptions:options];
  GRPCChannel *channel2 = [GRPCChannel channelWithHost:host callOptions:options];
  XCTAssertEqual(channel1, channel2);
  GRPCMutableCallOptions *options2 = [options mutableCopy];
  options2.additionalChannelArgs = [args copy];
  GRPCChannel *channel3 = [GRPCChannel channelWithHost:host callOptions:options2];
  XCTAssertEqual(channel1, channel3);
}

- (void)testDifferentHost {
  NSString *host1 = @"grpc-test.sandbox.googleapis.com";
  NSString *host2 = @"grpc-test2.sandbox.googleapis.com";
  NSString *host3 = @"http://grpc-test.sandbox.googleapis.com";
  NSString *host4 = @"dns://grpc-test.sandbox.googleapis.com";
  NSString *host5 = @"grpc-test.sandbox.googleapis.com:80";
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.userAgentPrefix = @"TestUAPrefix";
  NSMutableDictionary *args = [NSMutableDictionary new];
  args[@"abc"] = @"xyz";
  options.additionalChannelArgs = [args copy];
  GRPCChannel *channel1 = [GRPCChannel channelWithHost:host1 callOptions:options];
  GRPCChannel *channel2 = [GRPCChannel channelWithHost:host2 callOptions:options];
  GRPCChannel *channel3 = [GRPCChannel channelWithHost:host3 callOptions:options];
  GRPCChannel *channel4 = [GRPCChannel channelWithHost:host4 callOptions:options];
  GRPCChannel *channel5 = [GRPCChannel channelWithHost:host5 callOptions:options];
  XCTAssertNotEqual(channel1, channel2);
  XCTAssertNotEqual(channel1, channel3);
  XCTAssertNotEqual(channel1, channel4);
  XCTAssertNotEqual(channel1, channel5);
}

- (void)testDifferentChannelParameters {
  NSString *host = @"grpc-test.sandbox.googleapis.com";
  GRPCMutableCallOptions *options1 = [[GRPCMutableCallOptions alloc] init];
  options1.transportType = GRPCTransportTypeChttp2BoringSSL;
  NSMutableDictionary *args = [NSMutableDictionary new];
  args[@"abc"] = @"xyz";
  options1.additionalChannelArgs = [args copy];
  GRPCMutableCallOptions *options2 = [[GRPCMutableCallOptions alloc] init];
  options2.transportType = GRPCTransportTypeInsecure;
  options2.additionalChannelArgs = [args copy];
  GRPCMutableCallOptions *options3 = [[GRPCMutableCallOptions alloc] init];
  options3.transportType = GRPCTransportTypeChttp2BoringSSL;
  args[@"def"] = @"uvw";
  options3.additionalChannelArgs = [args copy];
  GRPCChannel *channel1 = [GRPCChannel channelWithHost:host callOptions:options1];
  GRPCChannel *channel2 = [GRPCChannel channelWithHost:host callOptions:options2];
  GRPCChannel *channel3 = [GRPCChannel channelWithHost:host callOptions:options3];
  XCTAssertNotEqual(channel1, channel2);
  XCTAssertNotEqual(channel1, channel3);
}

@end
