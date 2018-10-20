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

@class GRPCChannel;

/** Caching signature of a channel. */
@interface GRPCChannelConfiguration : NSObject<NSCopying>

/** The host that this channel is connected to. */
@property(copy, readonly) NSString *host;

/**
 * Options of the corresponding call. Note that only the channel-related options are of interest to
 * this class.
 */
@property(strong, readonly) GRPCCallOptions *callOptions;

/** Acquire the factory to generate a new channel with current configurations. */
@property(readonly) id<GRPCChannelFactory> channelFactory;

/** Acquire the dictionary of channel args with current configurations. */
@property(readonly) NSDictionary *channelArgs;

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

@end

/**
 * Manage the pool of connected channels. When a channel is no longer referenced by any call,
 * destroy the channel after a certain period of time elapsed.
 */
@interface GRPCChannelPool : NSObject

- (instancetype)init;

/**
 * Return a channel with a particular configuration. If the channel does not exist, execute \a
 * createChannel then add it in the pool. If the channel exists, increase its reference count.
 */
- (GRPCChannel *)channelWithConfiguration:(GRPCChannelConfiguration *)configuration;

/** Remove a channel from the pool. */
- (void)removeChannel:(GRPCChannel *)channel;

/** Clear all channels in the pool. */
- (void)removeAllChannels;

/** Clear all channels in the pool and destroy the channels. */
- (void)removeAndCloseAllChannels;

@end

NS_ASSUME_NONNULL_END
