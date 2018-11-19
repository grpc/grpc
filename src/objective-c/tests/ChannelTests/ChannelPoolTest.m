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
NSString *kDummyHost2 = @"dummy.host.2";
NSString *kDummyPath = @"/dummy/path";

@interface ChannelPoolTest : XCTestCase

@end

@implementation ChannelPoolTest

+ (void)setUp {
  grpc_init();
}

- (void)testCreateChannelAndCall {
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCPooledChannel *channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                            callOptions:options];
  XCTAssertNil(channel.wrappedChannel);
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  grpc_call *call = [channel unmanagedCallWithPath:kDummyPath
                                   completionQueue:cq callOptions:options];
  XCTAssert(call != NULL);
  XCTAssertNotNil(channel.wrappedChannel);
  [channel unrefUnmanagedCall:call];
  XCTAssertNil(channel.wrappedChannel);
}

- (void)testCacheChannel {
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCCallOptions *options1 = [[GRPCCallOptions alloc] init];
  GRPCCallOptions *options2 = [options1 copy];
  GRPCMutableCallOptions *options3 = [options1 mutableCopy];
  options3.transportType = GRPCTransportTypeInsecure;
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  GRPCPooledChannel *channel1 = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                             callOptions:options1];
  grpc_call *call1 = [channel1 unmanagedCallWithPath:kDummyPath
                                     completionQueue:cq
                                         callOptions:options1];
  GRPCPooledChannel *channel2 = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                             callOptions:options2];
  grpc_call *call2 = [channel2 unmanagedCallWithPath:kDummyPath
                                     completionQueue:cq
                                         callOptions:options2];
  GRPCPooledChannel *channel3 = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                             callOptions:options3];
  grpc_call *call3 = [channel3 unmanagedCallWithPath:kDummyPath
                                     completionQueue:cq
                                         callOptions:options3];
  GRPCPooledChannel *channel4 = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost2
                                                             callOptions:options1];
  grpc_call *call4 = [channel4 unmanagedCallWithPath:kDummyPath
                                     completionQueue:cq
                                         callOptions:options1];
  XCTAssertEqual(channel1.wrappedChannel, channel2.wrappedChannel);
  XCTAssertNotEqual(channel1.wrappedChannel, channel3.wrappedChannel);
  XCTAssertNotEqual(channel1.wrappedChannel, channel4.wrappedChannel);
  XCTAssertNotEqual(channel3.wrappedChannel, channel4.wrappedChannel);
  [channel1 unrefUnmanagedCall:call1];
  [channel2 unrefUnmanagedCall:call2];
  [channel3 unrefUnmanagedCall:call3];
  [channel4 unrefUnmanagedCall:call4];
}

- (void)testTimedDestroyChannel {
  const NSTimeInterval kDestroyDelay = 1.0;

  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  pool.destroyDelay = kDestroyDelay;
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCPooledChannel *channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                            callOptions:options];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  grpc_call *call = [channel unmanagedCallWithPath:kDummyPath
                                   completionQueue:cq callOptions:options];
  GRPCChannel *wrappedChannel = channel.wrappedChannel;

  [channel unrefUnmanagedCall:call];
  // Confirm channel is not destroyed at this time
  call = [channel unmanagedCallWithPath:kDummyPath
                        completionQueue:cq
                            callOptions:options];
  XCTAssertEqual(wrappedChannel, channel.wrappedChannel);

  [channel unrefUnmanagedCall:call];
  sleep(kDestroyDelay + 1);
  // Confirm channel is new at this time
  call = [channel unmanagedCallWithPath:kDummyPath
                        completionQueue:cq
                            callOptions:options];
  XCTAssertNotEqual(wrappedChannel, channel.wrappedChannel);

  // Confirm the new channel can create call
  call = [channel unmanagedCallWithPath:kDummyPath
                        completionQueue:cq
                            callOptions:options];
  XCTAssert(call != NULL);
  [channel unrefUnmanagedCall:call];
}

- (void)testPoolDisconnection {
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCPooledChannel *channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                            callOptions:options];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  grpc_call *call = [channel unmanagedCallWithPath:kDummyPath
                                   completionQueue:cq
                                       callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);
  GRPCChannel *wrappedChannel = channel.wrappedChannel;

  // Test a new channel is created by requesting a channel from pool
  [pool disconnectAllChannels];
  channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                          callOptions:options];
  call = [channel unmanagedCallWithPath:kDummyPath
                        completionQueue:cq
                            callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);
  XCTAssertNotEqual(wrappedChannel, channel.wrappedChannel);
  wrappedChannel = channel.wrappedChannel;

  // Test a new channel is created by requesting a new call from the previous proxy
  [pool disconnectAllChannels];
  grpc_call *call2 = [channel unmanagedCallWithPath:kDummyPath
                                    completionQueue:cq
                                        callOptions:options];
  XCTAssertNotNil(channel.wrappedChannel);
  XCTAssertNotEqual(channel.wrappedChannel, wrappedChannel);
  [channel unrefUnmanagedCall:call];
  [channel unrefUnmanagedCall:call2];
}

- (void)testUnrefCallFromStaleChannel {
  GRPCChannelPool *pool = [[GRPCChannelPool alloc] init];
  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  GRPCPooledChannel *channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                                            callOptions:options];
  GRPCCompletionQueue *cq = [GRPCCompletionQueue completionQueue];
  grpc_call *call = [channel unmanagedCallWithPath:kDummyPath
                                   completionQueue:cq
                                       callOptions:options];

  [pool disconnectAllChannels];
  channel = (GRPCPooledChannel *)[pool channelWithHost:kDummyHost
                                          callOptions:options];

  grpc_call *call2 = [channel unmanagedCallWithPath:kDummyPath
                                    completionQueue:cq
                                        callOptions:options];
  // Test unref the call of a stale channel will not cause the current channel going into timed
  // destroy state
  XCTAssertNotNil(channel.wrappedChannel);
  GRPCChannel *wrappedChannel = channel.wrappedChannel;
  [channel unrefUnmanagedCall:call];
  XCTAssertNotNil(channel.wrappedChannel);
  XCTAssertEqual(wrappedChannel, channel.wrappedChannel);
  // Test unref the call of the current channel will cause the channel going into timed destroy
  // state
  [channel unrefUnmanagedCall:call2];
  XCTAssertNil(channel.wrappedChannel);
}

@end
