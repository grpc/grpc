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
@class GRPCCallOptions;
@class GRPCChannelConfiguration;
struct grpc_channel_credentials;

/**
 * Each separate instance of this class represents at least one TCP connection to the provided host.
 */
@interface GRPCChannel : NSObject

- (nullable instancetype)init NS_UNAVAILABLE;

+ (nullable instancetype) new NS_UNAVAILABLE;

/**
 * Returns a channel connecting to \a host with options as \a callOptions. The channel may be new
 * or a cached channel that is already connected.
 */
+ (nullable instancetype)channelWithHost:(nonnull NSString *)host
                             callOptions:(nullable GRPCCallOptions *)callOptions;

/**
 * Create a channel object with the signature \a config.
 */
+ (nullable instancetype)createChannelWithConfiguration:(nonnull GRPCChannelConfiguration *)config;

/**
 * Get a grpc core call object from this channel.
 */
- (nullable grpc_call *)unmanagedCallWithPath:(nonnull NSString *)path
                              completionQueue:(nonnull GRPCCompletionQueue *)queue
                                  callOptions:(nonnull GRPCCallOptions *)callOptions;

/**
 * Increase the refcount of the channel. If the channel was timed to be destroyed, cancel the timer.
 */
- (void)ref;

/**
 * Decrease the refcount of the channel. If the refcount of the channel decrease to 0, the channel
 * is destroyed after 30 seconds.
 */
- (void)unref;

/**
 * Force the channel to be disconnected and destroyed immediately.
 */
- (void)disconnect;

// TODO (mxyan): deprecate with GRPCCall:closeOpenConnections
+ (void)closeOpenConnections;
@end
