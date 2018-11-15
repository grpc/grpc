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

@protocol GRPCChannelFactory;

@class GRPCCompletionQueue;
@class GRPCCallOptions;
@class GRPCChannelConfiguration;
struct grpc_channel_credentials;

NS_ASSUME_NONNULL_BEGIN

/** Caching signature of a channel. */
@interface GRPCChannelConfiguration : NSObject<NSCopying>

/** The host that this channel is connected to. */
@property(copy, readonly) NSString *host;

/**
 * Options of the corresponding call. Note that only the channel-related options are of interest to
 * this class.
 */
@property(readonly) GRPCCallOptions *callOptions;

/** Acquire the factory to generate a new channel with current configurations. */
@property(readonly) id<GRPCChannelFactory> channelFactory;

/** Acquire the dictionary of channel args with current configurations. */
@property(copy, readonly) NSDictionary *channelArgs;

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

@end

/**
 * Each separate instance of this class represents at least one TCP connection to the provided host.
 */
@interface GRPCChannel : NSObject

- (nullable instancetype)init NS_UNAVAILABLE;

+ (nullable instancetype) new NS_UNAVAILABLE;

/**
 * Create a channel with remote \a host and signature \a channelConfigurations. Destroy delay is
 * defaulted to 30 seconds.
 */
- (nullable instancetype)initWithChannelConfiguration:
    (GRPCChannelConfiguration *)channelConfiguration;

/**
 * Create a channel with remote \a host, signature \a channelConfigurations, and destroy delay of
 * \a destroyDelay.
 */
- (nullable instancetype)initWithChannelConfiguration:
                             (GRPCChannelConfiguration *)channelConfiguration
                                         destroyDelay:(NSTimeInterval)destroyDelay
    NS_DESIGNATED_INITIALIZER;

/**
 * Create a grpc core call object from this channel. The channel's refcount is added by 1. If no
 * call is created, NULL is returned, and if the reason is because the channel is already
 * disconnected, \a disconnected is set to YES. When the returned call is unreffed, the caller is
 * obligated to call \a unref method once. \a disconnected may be null.
 */
- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue
                                  callOptions:(GRPCCallOptions *)callOptions
                                 disconnected:(BOOL *_Nullable)disconnected;

/**
 * Unref the channel when a call is done. It also decreases the channel's refcount. If the refcount
 * of the channel decreases to 0, the channel is destroyed after the destroy delay.
 */
- (void)unref;

/**
 * Force the channel to be disconnected and destroyed.
 */
- (void)disconnect;

/**
 * Return whether the channel is already disconnected.
 */
@property(readonly) BOOL disconnected;

@end

NS_ASSUME_NONNULL_END
