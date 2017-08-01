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
struct grpc_channel_credentials;


/**
 * Each separate instance of this class represents at least one TCP connection to the provided host.
 */
@interface GRPCChannel : NSObject

@property(nonatomic, readonly, nonnull) struct grpc_channel *unmanagedChannel;

- (nullable instancetype)init NS_UNAVAILABLE;

/**
 * Creates a secure channel to the specified @c host using default credentials and channel
 * arguments. If certificates could not be found to create a secure channel, then @c nil is
 * returned.
 */
+ (nullable GRPCChannel *)secureChannelWithHost:(nonnull NSString *)host;

/**
 * Creates a secure channel to the specified @c host using Cronet as a transport mechanism.
 */
#ifdef GRPC_COMPILE_WITH_CRONET
+ (nullable GRPCChannel *)secureCronetChannelWithHost:(nonnull NSString *)host
                                          channelArgs:(nonnull NSDictionary *)channelArgs;
#endif
/**
 * Creates a secure channel to the specified @c host using the specified @c credentials and
 * @c channelArgs. Only in tests should @c GRPC_SSL_TARGET_NAME_OVERRIDE_ARG channel arg be set.
 */
+ (nonnull GRPCChannel *)secureChannelWithHost:(nonnull NSString *)host
    credentials:(nonnull struct grpc_channel_credentials *)credentials
    channelArgs:(nullable NSDictionary *)channelArgs;

/**
 * Creates an insecure channel to the specified @c host using the specified @c channelArgs.
 */
+ (nonnull GRPCChannel *)insecureChannelWithHost:(nonnull NSString *)host
                                     channelArgs:(nullable NSDictionary *)channelArgs;

- (nullable grpc_call *)unmanagedCallWithPath:(nonnull NSString *)path
                                   serverName:(nonnull NSString *)serverName
                              completionQueue:(nonnull GRPCCompletionQueue *)queue;
@end
