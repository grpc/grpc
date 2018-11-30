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
@class GRPCWrappedCall;

/**
 * A proxied channel object that can be retained and creates GRPCWrappedCall object from. If a
 * raw channel is not present (i.e. no tcp connection to the server) when a GRPCWrappedCall object
 * is requested, it issues a connection/reconnection. The behavior of this object is to mimic that
 * of gRPC core's channel object.
 */
@interface GRPCPooledChannel : NSObject

- (nullable instancetype)init NS_UNAVAILABLE;

+ (nullable instancetype) new NS_UNAVAILABLE;

/**
 * Initialize with an actual channel object \a channel and a reference to the channel pool.
 */
- (nullable instancetype)initWithChannelConfiguration:
                             (GRPCChannelConfiguration *)channelConfiguration;

/**
 * Create a GRPCWrappedCall object (grpc_call) from this channel. If channel is disconnected, get a
 * new channel object from the channel pool.
 */
- (nullable GRPCWrappedCall *)wrappedCallWithPath:(NSString *)path
                                  completionQueue:(GRPCCompletionQueue *)queue
                                      callOptions:(GRPCCallOptions *)callOptions;

/**
 * Notify the pooled channel that a wrapped call object is no longer referenced and will be
 * dealloc'ed.
 */
- (void)notifyWrappedCallDealloc:(GRPCWrappedCall *)wrappedCall;

/**
 * Force the channel to disconnect immediately. Subsequent calls to unmanagedCallWithPath: will
 * attempt to reconnect to the remote channel.
 */
- (void)disconnect;

@end

/** Test-only interface for \a GRPCPooledChannel. */
@interface GRPCPooledChannel (Test)

/**
 * Initialize a pooled channel with non-default destroy delay for testing purpose.
 */
- (nullable instancetype)initWithChannelConfiguration:
(GRPCChannelConfiguration *)channelConfiguration
                                         destroyDelay:(NSTimeInterval)destroyDelay;

/**
 * Return the pointer to the raw channel wrapped.
 */
@property(atomic, readonly) GRPCChannel *wrappedChannel;

@end

/**
 * Manage the pool of connected channels. When a channel is no longer referenced by any call,
 * destroy the channel after a certain period of time elapsed.
 */
@interface GRPCChannelPool : NSObject

- (nullable instancetype)init NS_UNAVAILABLE;

+ (nullable instancetype) new NS_UNAVAILABLE;

/**
 * Get the global channel pool.
 */
+ (nullable instancetype)sharedInstance;

/**
 * Return a channel with a particular configuration. The channel may be a cached channel.
 */
- (GRPCPooledChannel *)channelWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions;

/**
 * Disconnect all channels in this pool.
 */
- (void)disconnectAllChannels;

@end

/** Test-only interface for \a GRPCChannelPool. */
@interface GRPCChannelPool (Test)

/**
 * Get an instance of pool isolated from the global shared pool with channels' destroy delay being
 * \a destroyDelay.
 */
- (nullable instancetype)initTestPool;

@end

NS_ASSUME_NONNULL_END
