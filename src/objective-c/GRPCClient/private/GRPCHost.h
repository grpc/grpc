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

#import <grpc/impl/codegen/compression_types.h>

NS_ASSUME_NONNULL_BEGIN

@class GRPCCompletionQueue;
struct grpc_call;
struct grpc_channel_credentials;

@interface GRPCHost : NSObject

+ (void)flushChannelCache;
+ (void)resetAllHostSettings;

@property(nonatomic, readonly) NSString *address;
@property(nonatomic, copy, nullable) NSString *userAgentPrefix;
@property(nonatomic, nullable) struct grpc_channel_credentials *channelCreds;
@property(nonatomic) grpc_compression_algorithm compressAlgorithm;
@property(nonatomic) int keepaliveInterval;
@property(nonatomic) int keepaliveTimeout;
@property(nonatomic) id logContext;
@property(nonatomic) BOOL retryEnabled;

@property(nonatomic) unsigned int minConnectTimeout;
@property(nonatomic) unsigned int initialConnectBackoff;
@property(nonatomic) unsigned int maxConnectBackoff;

/** The following properties should only be modified for testing: */

@property(nonatomic, getter=isSecure) BOOL secure;

@property(nonatomic, copy, nullable) NSString *hostNameOverride;

/** The default response size limit is 4MB. Set this to override that default. */
@property(nonatomic, strong, nullable) NSNumber *responseSizeLimitOverride;

- (nullable instancetype)init NS_UNAVAILABLE;
/** Host objects initialized with the same address are the same. */
+ (nullable instancetype)hostWithAddress:(NSString *)address;
- (nullable instancetype)initWithAddress:(NSString *)address NS_DESIGNATED_INITIALIZER;
- (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                     error:(NSError **)errorPtr;

/** Create a grpc_call object to the provided path on this host. */
- (nullable struct grpc_call *)unmanagedCallWithPath:(NSString *)path
                                          serverName:(NSString *)serverName
                                             timeout:(NSTimeInterval)timeout
                                     completionQueue:(GRPCCompletionQueue *)queue;

// TODO: There's a race when a new RPC is coming through just as an existing one is getting
// notified that there's no connectivity. If connectivity comes back at that moment, the new RPC
// will have its channel destroyed by the other RPC, and will never get notified of a problem, so
// it'll hang (the C layer logs a timeout, with exponential back off). One solution could be to pass
// the GRPCChannel to the GRPCCall, renaming this as "disconnectChannel:channel", which would only
// act on that specific channel.
- (void)disconnect;
@end

NS_ASSUME_NONNULL_END
