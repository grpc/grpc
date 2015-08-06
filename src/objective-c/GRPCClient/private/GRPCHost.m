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

#import "GRPCChannel.h"
#import "GRPCCompletionQueue.h"
#import "GRPCSecureChannel.h"
#import "GRPCUnsecuredChannel.h"

@interface GRPCHost ()
// TODO(mlumish): Investigate whether caching channels with strong links is a good idea.
@property(nonatomic, strong) GRPCChannel *channel;
@end

@implementation GRPCHost

+ (instancetype)hostWithAddress:(NSString *)address {
  return [[self alloc] initWithAddress:address];
}

- (instancetype)init {
  return [self initWithAddress:nil];
}

// Default initializer.
- (instancetype)initWithAddress:(NSString *)address {

  // To provide a default port, we try to interpret the address. If it's just a host name without
  // scheme and without port, we'll use port 443. If it has a scheme, we pass it untouched to the C
  // gRPC library.
  // TODO(jcanizales): Add unit tests for the types of addresses we want to let pass untouched.
  NSURL *hostURL = [NSURL URLWithString:[@"https://" stringByAppendingString:address]];
  if (hostURL && !hostURL.port) {
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
  return self;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path completionQueue:(GRPCCompletionQueue *)queue {
  if (!queue || !path || !self.channel) {
    return NULL;
  }
  return grpc_channel_create_call(self.channel.unmanagedChannel,
                                  NULL, GRPC_PROPAGATE_DEFAULTS,
                                  queue.unmanagedQueue,
                                  path.UTF8String,
                                  self.hostName.UTF8String,
                                  gpr_inf_future(GPR_CLOCK_REALTIME));
}

- (GRPCChannel *)channel {
  // Create it lazily, because we don't want to open a connection just because someone is
  // configuring a host.
  if (!_channel) {
    if (_secure) {
      _channel = [[GRPCSecureChannel alloc] initWithHost:_address
                                      pathToCertificates:_pathToCertificates
                                        hostNameOverride:_hostNameOverride];
    } else {
      _channel = [[GRPCUnsecuredChannel alloc] initWithHost:_address];
    }
  }
  return _channel;
}

- (NSString *)hostName {
  // TODO(jcanizales): Default to nil instead of _address when Issue #2635 is clarified.
  return _hostNameOverride ?: _address;
}

// TODO(jcanizales): Don't let set |secure| to |NO| if |pathToCertificates| or |hostNameOverride|
// have been set. Don't let set either of the latter if |secure| has been set to |NO|.

@end
