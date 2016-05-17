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

NS_ASSUME_NONNULL_BEGIN

@class GRPCCompletionQueue;
struct grpc_call;
struct grpc_channel_credentials;

@interface GRPCHost : NSObject

@property(nonatomic, readonly) NSString *address;
@property(nonatomic, copy, nullable) NSString *userAgentPrefix;
@property(nonatomic, nullable) struct grpc_channel_credentials *channelCreds;

/** The following properties should only be modified for testing: */

@property(nonatomic, getter=isSecure) BOOL secure;

@property(nonatomic, copy, nullable) NSString *hostNameOverride;

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
