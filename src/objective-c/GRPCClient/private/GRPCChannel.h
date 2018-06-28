/*
 *
 * Copyright 2015 gRPC authors.
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

#import <Foundation/Foundation.h>

#include <grpc/grpc.h>

@class GRPCCompletionQueue;

/**
 * Each separate instance of this class represents at least one TCP connection to the provided host.
 */
@interface GRPCChannel : NSObject

- (nullable instancetype)init NS_UNAVAILABLE;

- (nullable instancetype)initWithUnmanagedChannel:(nullable grpc_channel *)unmanagedChannel
                                      channelArgs:(nullable grpc_channel_args *)channelArgs;

- (nullable grpc_call *)unmanagedCallWithPath:(nonnull NSString *)path
                                   serverName:(nonnull NSString *)serverName
                                      timeout:(NSTimeInterval)timeout
                              completionQueue:(nonnull GRPCCompletionQueue *)queue;
@end

grpc_channel_args* _Nullable BuildChannelArgs(NSDictionary * _Nullable dictionary);
