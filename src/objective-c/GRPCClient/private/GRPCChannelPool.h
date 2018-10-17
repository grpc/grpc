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

@interface GRPCChannelConfiguration : NSObject<NSCopying>

@property(copy, readonly) NSString *host;
@property(strong, readonly) GRPCCallOptions *callOptions;

@property(readonly) id<GRPCChannelFactory> channelFactory;
@property(readonly) NSDictionary *channelArgs;

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

@end

/**
 * Manage the pool of connected channels. When a channel is no longer referenced by any call,
 * destroy the channel after a certain period of time elapsed.
 */
@interface GRPCChannelPool : NSObject

- (instancetype)init;

- (instancetype)initWithChannelDestroyDelay:(NSTimeInterval)channelDestroyDelay;

/**
 * Return a channel with a particular configuration. If the channel does not exist, execute \a
 * createChannel then add it in the pool. If the channel exists, increase its reference count.
 */
- (GRPCChannel *)channelWithConfiguration:(GRPCChannelConfiguration *)configuration
                            createChannel:(GRPCChannel * (^)(void))createChannel;

/** Decrease a channel's refcount. */
- (void)unrefChannelWithConfiguration:configuration;

/** Clear all channels in the pool. */
- (void)clear;

@end

NS_ASSUME_NONNULL_END
