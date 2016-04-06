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

#import "GRPCHost.h"

#include <grpc/grpc.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCCall+ChannelArg.h>

#import "GRPCChannel.h"
#import "GRPCCompletionQueue.h"
#import "NSDictionary+GRPC.h"

NS_ASSUME_NONNULL_BEGIN

// TODO(jcanizales): Generate the version in a standalone header, from templates. Like
// templates/src/core/surface/version.c.template .
#define GRPC_OBJC_VERSION_STRING @"0.13.0"

@implementation GRPCHost {
  // TODO(mlumish): Investigate whether caching channels with strong links is a good idea.
  GRPCChannel *_channel;
}

+ (nullable instancetype)hostWithAddress:(NSString *)address {
  return [[self alloc] initWithAddress:address];
}

// Default initializer.
- (nullable instancetype)initWithAddress:(NSString *)address {
  if (!address) {
    return nil;
  }

  // To provide a default port, we try to interpret the address. If it's just a host name without
  // scheme and without port, we'll use port 443. If it has a scheme, we pass it untouched to the C
  // gRPC library.
  // TODO(jcanizales): Add unit tests for the types of addresses we want to let pass untouched.
  NSURL *hostURL = [NSURL URLWithString:[@"https://" stringByAppendingString:address]];
  if (hostURL.host && !hostURL.port) {
    address = [hostURL.host stringByAppendingString:@":443"];
  }

  // Look up the GRPCHost in the cache.
  static NSMutableDictionary *hostCache;
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    hostCache = [NSMutableDictionary dictionary];
  });
  @synchronized(hostCache) {
    GRPCHost *cachedHost = hostCache[address];
    if (cachedHost) {
      return cachedHost;
    }

    if ((self = [super init])) {
      _address = address;
      _secure = YES;
      hostCache[address] = self;
    }
  }
  return self;
}

- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue {
  GRPCChannel *channel;
  // This is racing -[GRPCHost disconnect].
  @synchronized(self) {
    if (!_channel) {
      _channel = [self newChannel];
    }
    channel = _channel;
  }
  return [channel unmanagedCallWithPath:path completionQueue:queue];
}

- (NSDictionary *)channelArgs {
  NSMutableDictionary *args = [NSMutableDictionary dictionary];

  // TODO(jcanizales): Add OS and device information (see
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#user-agents ).
  NSString *userAgent = @"grpc-objc/" GRPC_OBJC_VERSION_STRING;
  if (_userAgentPrefix) {
    userAgent = [_userAgentPrefix stringByAppendingFormat:@" %@", userAgent];
  }
  args[@GRPC_ARG_PRIMARY_USER_AGENT_STRING] = userAgent;

  if (_secure && _hostNameOverride) {
    args[@GRPC_SSL_TARGET_NAME_OVERRIDE_ARG] = _hostNameOverride;
  }
  return args;
}

- (GRPCChannel *)newChannel {
  NSDictionary *args = [self channelArgs];
  if (_secure) {
    return [GRPCChannel secureChannelWithHost:_address
                           pathToCertificates:_pathToCertificates
                                  channelArgs:args];
  } else {
    return [GRPCChannel insecureChannelWithHost:_address channelArgs:args];
  }
}

- (NSString *)hostName {
  // TODO(jcanizales): Default to nil instead of _address when Issue #2635 is clarified.
  return _hostNameOverride ?: _address;
}

- (void)disconnect {
  // This is racing -[GRPCHost unmanagedCallWithPath:completionQueue:].
  @synchronized(self) {
    _channel = nil;
  }
}

// TODO(jcanizales): Don't let set |secure| to |NO| if |pathToCertificates| or |hostNameOverride|
// have been set. Don't let set either of the latter if |secure| has been set to |NO|.

@end

NS_ASSUME_NONNULL_END
