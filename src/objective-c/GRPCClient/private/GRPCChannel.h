/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
+ (nullable GRPCChannel *)secureCronetChannelWithHost:(NSString *)host
                                          channelArgs:(NSDictionary *)channelArgs;
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
                              completionQueue:(nonnull GRPCCompletionQueue *)queue;
@end
