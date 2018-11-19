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

/**
 * Signature for the channel. If two channel's signatures are the same, they share the same
 * underlying \a GRPCChannel object.
 */

#import <GRPCClient/GRPCCallOptions.h>

#import "GRPCChannelFactory.h"

NS_ASSUME_NONNULL_BEGIN

@protocol GRPCChannel;
@class GRPCChannel;
@class GRPCChannelPool;
@class GRPCCompletionQueue;
@class GRPCChannelConfiguration;

/**
 * Channel proxy that can be retained and automatically reestablish connection when the channel is
 * disconnected.
 */
@interface GRPCPooledChannel : NSObject

/**
 * Initialize with an actual channel object \a channel and a reference to the channel pool.
 */
- (nullable instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)channelConfiguration
                                          channelPool:(GRPCChannelPool *)channelPool;


/**
 * Create a grpc core call object (grpc_call) from this channel. If channel is disconnected, get a
 * new channel object from the channel pool.
 */
- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue
                                  callOptions:(GRPCCallOptions *)callOptions;

/**
 * Return ownership and destroy the grpc_call object created by
 * \a unmanagedCallWithPath:completionQueue:callOptions: and decrease channel refcount. If refcount
 * of the channel becomes 0, return the channel object to channel pool.
 */
- (void)unrefUnmanagedCall:(grpc_call *)unmanagedCall;

/**
 * Force the channel to disconnect immediately.
 */
- (void)disconnect;

// The following methods and properties are for test only

/**
 * Return the pointer to the real channel wrapped by the proxy.
 */
@property(atomic, readonly) GRPCChannel *wrappedChannel;

@end

/**
 * Manage the pool of connected channels. When a channel is no longer referenced by any call,
 * destroy the channel after a certain period of time elapsed.
 */
@interface GRPCChannelPool : NSObject

/**
 * Get the global channel pool.
 */
+ (nullable instancetype)sharedInstance;

/**
 * Return a channel with a particular configuration. The channel may be a cached channel.
 */
- (GRPCPooledChannel *)channelWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

/**
 * This method is deprecated.
 *
 * Destroy all open channels and close their connections.
 */
- (void)closeOpenConnections;

// Test-only methods below

/**
 * Get an instance of pool isolated from the global shared pool. This method is for test only.
 * Global pool should be used in production.
 */
- (nullable instancetype)init;

/**
 * Simulate a network transition event and destroy all channels. This method is for internal and
 * test only.
 */
- (void)disconnectAllChannels;

/**
 * Set the destroy delay of channels. A channel should be destroyed if it stayed idle (no active
 * call on it) for this period of time. This property is for test only.
 */
@property(atomic) NSTimeInterval destroyDelay;

@end

NS_ASSUME_NONNULL_END
