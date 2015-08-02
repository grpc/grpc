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

#import "GRPCChannel.h"
#import "GRPCSecureChannel.h"
#import "GRPCUnsecuredChannel.h"

@interface GRPCHost ()
// TODO(mlumish): Investigate whether a caching channels with strong links is a good idea.
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

  // Verify and normalize the address, and decide whether to use SSL.
  if (![address rangeOfString:@"://"].length) {
    // No scheme provided; assume https.
    address = [@"https://" stringByAppendingString:address];
  }
  NSURL *hostURL = [NSURL URLWithString:address];
  if (!hostURL) {
    [NSException raise:NSInvalidArgumentException format:@"Invalid URL: %@", address];
  }
  NSString *scheme = hostURL.scheme;
  if (![scheme isEqualToString:@"https"] && ![scheme isEqualToString:@"http"]) {
    [NSException raise:NSInvalidArgumentException format:@"URL scheme %@ isn't supported.", scheme];
  }
  NSNumber *port = hostURL.port ?: [scheme isEqualToString:@"https"] ? @443 : @80;
  address = [@[hostURL.host, port] componentsJoinedByString:@":"];

  // Look up the GRPCHost in the cache.
  // TODO(jcanizales): Make this cache thread-safe.
  static NSMutableDictionary *hostCache;
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    hostCache = [NSMutableDictionary dictionary];
  });
  if (hostCache[address]) {
    // We could verify here that the cached host uses the same protocol that we're expecting. But
    // picking HTTP by adding the scheme to the address is going away (to make the use of insecure
    // channels less subtle), so it's not worth it now.
    return hostCache[address];
  }

  if ((self = [super init])) {
    _address = address;
    _secure = [scheme isEqualToString:@"https"];
    hostCache[address] = self;
  }
  return self;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path completionQueue:(GRPCCompletionQueue *)queue {
  if (!queue || !path) {
    return NULL;
  }
  return grpc_channel_create_call(self.channel.unmanagedChannel,
                                  queue.unmanagedQueue,
                                  path.UTF8String,
                                  self.hostName.UTF8String,
                                  gpr_inf_future(GPR_CLOCK_REALTIME));
}

- (GRPCChannel *)channel {
  // Create it lazily.
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

@end
