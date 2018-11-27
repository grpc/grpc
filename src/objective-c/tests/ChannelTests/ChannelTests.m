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
#import "../../GRPCClient/private/GRPCChannelPool.h"
#import "../../GRPCClient/private/GRPCCompletionQueue.h"

/*
#define TEST_TIMEOUT 8

@interface GRPCChannelFake : NSObject

- (instancetype)initWithCreateExpectation:(XCTestExpectation *)createExpectation
                         unrefExpectation:(XCTestExpectation *)unrefExpectation;

- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue
                                  callOptions:(GRPCCallOptions *)callOptions;

- (void)destroyUnmanagedCall:(grpc_call *)unmanagedCall;

@end

@implementation GRPCChannelFake {
  __weak XCTestExpectation *_createExpectation;
  __weak XCTestExpectation *_unrefExpectation;
  long _grpcCallCounter;
}

- (nullable instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration
*)channelConfiguration { return nil;
}

- (instancetype)initWithCreateExpectation:(XCTestExpectation *)createExpectation
                         unrefExpectation:(XCTestExpectation *)unrefExpectation {
  if ((self = [super init])) {
    _createExpectation = createExpectation;
    _unrefExpectation = unrefExpectation;
    _grpcCallCounter = 0;
  }
  return self;
}

- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue
                                  callOptions:(GRPCCallOptions *)callOptions {
  if (_createExpectation) [_createExpectation fulfill];
  return (grpc_call *)(++_grpcCallCounter);
}

- (void)destroyUnmanagedCall:(grpc_call *)unmanagedCall {
  if (_unrefExpectation) [_unrefExpectation fulfill];
}

@end

@interface GRPCChannelPoolFake : NSObject

- (instancetype)initWithDelayedDestroyExpectation:(XCTestExpectation *)delayedDestroyExpectation;

- (GRPCChannel *)rawChannelWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

- (void)delayedDestroyChannel;

@end

@implementation GRPCChannelPoolFake {
  __weak XCTestExpectation *_delayedDestroyExpectation;
}

- (instancetype)initWithDelayedDestroyExpectation:(XCTestExpectation *)delayedDestroyExpectation {
  if ((self = [super init])) {
    _delayedDestroyExpectation = delayedDestroyExpectation;
  }
  return self;
}

- (void)delayedDestroyChannel {
  if (_delayedDestroyExpectation) [_delayedDestroyExpectation fulfill];
}

@end */

@interface ChannelTests : XCTestCase

@end

@implementation ChannelTests

+ (void)setUp {
  grpc_init();
}

@end
