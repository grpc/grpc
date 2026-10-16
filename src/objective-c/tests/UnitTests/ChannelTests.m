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
#import "../../GRPCClient/private/GRPCCore/GRPCChannel.h"
#import "../../GRPCClient/private/GRPCCore/GRPCChannelPool+Test.h"
#import "../../GRPCClient/private/GRPCCore/GRPCChannelPool.h"
#import "../../GRPCClient/private/GRPCCore/GRPCCompletionQueue.h"
#import "../../GRPCClient/private/GRPCCore/GRPCWrappedCall.h"

static NSString *kPhonyHost = @"phony.host";
static NSString *kPhonyPath = @"/phony/path";

@interface ChannelTests : XCTestCase

@end

@implementation ChannelTests

+ (void)setUp {
  grpc_init();
}

- (void)testPooledChannelCreatingChannel {
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCChannelConfiguration *config = [[GRPCChannelConfiguration alloc] initWithHost:kPhonyHost
                                                                        callOptions:options];
  GRPCPooledChannel *channel = [[GRPCPooledChannel alloc] initWithChannelConfiguration:config];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  GRPCWrappedCall *wrappedCall = [channel wrappedCallWithPath:kPhonyPath
                                              completionQueue:cq
                                                  callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);
  (void)wrappedCall;
}

- (void)testTimedDestroyChannel {
  const NSTimeInterval kDestroyDelay = 1.0;
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCChannelConfiguration *config = [[GRPCChannelConfiguration alloc] initWithHost:kPhonyHost
                                                                        callOptions:options];
  GRPCPooledChannel *channel =
      [[GRPCPooledChannel alloc] initWithChannelConfiguration:config destroyDelay:kDestroyDelay];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  GRPCWrappedCall *wrappedCall;
  GRPCChannel *wrappedChannel;
  @autoreleasepool {
    wrappedCall = [channel wrappedCallWithPath:kPhonyPath completionQueue:cq callOptions:options];
    XCTAssertNotNil(channel.wrappedChannel);

    // Unref and ref channel immediately; expect using the same raw channel.
    wrappedChannel = channel.wrappedChannel;

    wrappedCall = nil;
    wrappedCall = [channel wrappedCallWithPath:kPhonyPath completionQueue:cq callOptions:options];
    XCTAssertEqual(channel.wrappedChannel, wrappedChannel);

    // Unref and ref channel after destroy delay; expect a new raw channel.
    wrappedCall = nil;
  }
  sleep(kDestroyDelay + 1);
  XCTAssertNil(channel.wrappedChannel);
  wrappedCall = [channel wrappedCallWithPath:kPhonyPath completionQueue:cq callOptions:options];
  XCTAssertNotEqual(channel.wrappedChannel, wrappedChannel);
}

- (void)testDisconnect {
  const NSTimeInterval kDestroyDelay = 1.0;
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCChannelConfiguration *config = [[GRPCChannelConfiguration alloc] initWithHost:kPhonyHost
                                                                        callOptions:options];
  GRPCPooledChannel *channel =
      [[GRPCPooledChannel alloc] initWithChannelConfiguration:config destroyDelay:kDestroyDelay];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  GRPCWrappedCall *wrappedCall = [channel wrappedCallWithPath:kPhonyPath
                                              completionQueue:cq
                                                  callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);

  // Disconnect; expect wrapped channel to be dropped
  [channel disconnect];
  XCTAssertNil(channel.wrappedChannel);

  // Create a new call and unref the old call; confirm that destroy of the old call does not make
  // the channel disconnect, even after the destroy delay.
  GRPCWrappedCall *wrappedCall2 = [channel wrappedCallWithPath:kPhonyPath
                                               completionQueue:cq
                                                   callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);
  GRPCChannel *wrappedChannel = channel.wrappedChannel;
  wrappedCall = nil;
  sleep(kDestroyDelay + 1);
  XCTAssertNotNil(channel.wrappedChannel);
  XCTAssertEqual(wrappedChannel, channel.wrappedChannel);
  (void)wrappedCall2;
}

@end
