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

#import "GRPCChannel.h"

#include <grpc/grpc.h>

#import "GRPCSecureChannel.h"
#import "GRPCUnsecuredChannel.h"

@implementation GRPCChannel

+ (instancetype)channelToHost:(NSString *)host {
  // TODO(mlumish): Investigate whether a cache with strong links is a good idea
  static NSMutableDictionary *channelCache;
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    channelCache = [NSMutableDictionary dictionary];
  });
  GRPCChannel *channel = channelCache[host];
  if (!channel) {
    channel = [[self alloc] initWithHost:host];
    channelCache[host] = channel;
  }
  return channel;
}

- (instancetype)init {
  return [self initWithHost:nil];
}

- (instancetype)initWithHost:(NSString *)host {
  if (![host containsString:@"://"]) {
    // No scheme provided; assume https.
    host = [@"https://" stringByAppendingString:host];
  }
  NSURL *hostURL = [NSURL URLWithString:host];
  if (!hostURL) {
    [NSException raise:NSInvalidArgumentException format:@"Invalid URL: %@", host];
  }
  if ([hostURL.scheme isEqualToString:@"https"]) {
    host = [@[hostURL.host, hostURL.port ?: @443] componentsJoinedByString:@":"];
    return [[GRPCSecureChannel alloc] initWithHost:host];
  }
  if ([hostURL.scheme isEqualToString:@"http"]) {
    host = [@[hostURL.host, hostURL.port ?: @80] componentsJoinedByString:@":"];
    return [[GRPCUnsecuredChannel alloc] initWithHost:host];
  }
  [NSException raise:NSInvalidArgumentException
              format:@"URL scheme %@ isn't supported.", hostURL.scheme];
  return nil; // silence warning.
}

- (instancetype)initWithChannel:(struct grpc_channel *)unmanagedChannel {
  if ((self = [super init])) {
    _unmanagedChannel = unmanagedChannel;
  }
  return self;
}

- (void)dealloc {
  // _unmanagedChannel is NULL when deallocating an object of the base class (because the
  // initializer returns a different object).
  if (_unmanagedChannel) {
    // TODO(jcanizales): Be sure to add a test with a server that closes the connection prematurely,
    // as in the past that made this call to crash.
    grpc_channel_destroy(_unmanagedChannel);
  }
}
@end
