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

#import "GRPCHost.h"

#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCCall+Cronet.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#import "GRPCChannel.h"
#import "GRPCCompletionQueue.h"
#import "GRPCConnectivityMonitor.h"
#import "NSDictionary+GRPC.h"
#import "version.h"
#import "GRPCChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "GRPCCronetChannelFactory.h"

NS_ASSUME_NONNULL_BEGIN

static NSMutableDictionary *kHostCache;

@implementation GRPCHost {
  // TODO(mlumish): Investigate whether caching channels with strong links is a good idea.
  GRPCChannel *_channel;
}

+ (nullable instancetype)hostWithAddress:(NSString *)address {
  return [[self alloc] initWithAddress:address];
}

- (void)dealloc {
  if (_channelCreds != nil) {
    grpc_channel_credentials_release(_channelCreds);
  }
#ifndef GRPC_CFSTREAM
  [GRPCConnectivityMonitor unregisterObserver:self];
#endif
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
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    kHostCache = [NSMutableDictionary dictionary];
  });
  @synchronized(kHostCache) {
    GRPCHost *cachedHost = kHostCache[address];
    if (cachedHost) {
      return cachedHost;
    }

    if ((self = [super init])) {
      _address = address;
      _channelFactory = [GRPCSecureChannelFactory factoryWithPEMRootCerts:nil privateKey:nil certChain:nil error:nil];
      kHostCache[address] = self;
      _compressAlgorithm = GRPC_COMPRESS_NONE;
      _retryEnabled = YES;
    }
#ifndef GRPC_CFSTREAM
    [GRPCConnectivityMonitor registerObserver:self selector:@selector(connectivityChange:)];
#endif
  }
  return self;
}

+ (void)flushChannelCache {
  @synchronized(kHostCache) {
    [kHostCache enumerateKeysAndObjectsUsingBlock:^(id _Nonnull key, GRPCHost *_Nonnull host,
                                                    BOOL *_Nonnull stop) {
      [host disconnect];
    }];
  }
}

+ (void)resetAllHostSettings {
  @synchronized(kHostCache) {
    kHostCache = [NSMutableDictionary dictionary];
  }
}

- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                                   serverName:(NSString *)serverName
                                      timeout:(NSTimeInterval)timeout
                              completionQueue:(GRPCCompletionQueue *)queue {
  // The __block attribute is to allow channel take refcount inside @synchronized block. Without
  // this attribute, retain of channel object happens after objc_sync_exit in release builds, which
  // may result in channel released before used. See grpc/#15033.
  __block GRPCChannel *channel;
  // This is racing -[GRPCHost disconnect].
  @synchronized(self) {
    if (!_channel) {
      _channel = [self newChannel];
    }
    channel = _channel;
  }
  return [channel unmanagedCallWithPath:path
                             serverName:serverName
                                timeout:timeout
                        completionQueue:queue];
}

- (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                     error:(NSError **)errorPtr {
  _channelFactory = [GRPCSecureChannelFactory factoryWithPEMRootCerts:pemRootCerts
                                                           privateKey:pemPrivateKey
                                                            certChain:pemCertChain
                                                                error:errorPtr];
  return (*errorPtr == nil);
}

- (NSMutableDictionary *)channelArgs {
  NSMutableDictionary *args = [NSMutableDictionary dictionary];

  // TODO(jcanizales): Add OS and device information (see
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#user-agents ).
  NSString *userAgent = @"grpc-objc/" GRPC_OBJC_VERSION_STRING;
  if (_userAgentPrefix) {
    userAgent = [_userAgentPrefix stringByAppendingFormat:@" %@", userAgent];
  }
  args[@GRPC_ARG_PRIMARY_USER_AGENT_STRING] = userAgent;

  if (/*****_secure && *****/_hostNameOverride) {
    args[@GRPC_SSL_TARGET_NAME_OVERRIDE_ARG] = _hostNameOverride;
  }

  if (_responseSizeLimitOverride) {
    args[@GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH] = _responseSizeLimitOverride;
  }

  if (_compressAlgorithm != GRPC_COMPRESS_NONE) {
    args[@GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM] = [NSNumber numberWithInt:_compressAlgorithm];
  }

  if (_keepaliveInterval != 0) {
    args[@GRPC_ARG_KEEPALIVE_TIME_MS] = [NSNumber numberWithInt:_keepaliveInterval];
    args[@GRPC_ARG_KEEPALIVE_TIMEOUT_MS] = [NSNumber numberWithInt:_keepaliveTimeout];
  }

  id logContext = self.logContext;
  if (logContext != nil) {
    args[@GRPC_ARG_MOBILE_LOG_CONTEXT] = logContext;
  }

  if (_retryEnabled == NO) {
    args[@GRPC_ARG_ENABLE_RETRIES] = [NSNumber numberWithInt:0];
  }

  if (_minConnectTimeout > 0) {
    args[@GRPC_ARG_MIN_RECONNECT_BACKOFF_MS] = [NSNumber numberWithInt:_minConnectTimeout];
  }
  if (_initialConnectBackoff > 0) {
    args[@GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS] = [NSNumber numberWithInt:_initialConnectBackoff];
  }
  if (_maxConnectBackoff > 0) {
    args[@GRPC_ARG_MAX_RECONNECT_BACKOFF_MS] = [NSNumber numberWithInt:_maxConnectBackoff];
  }

  return args;
}

- (GRPCChannel *)newChannel {
  // TODO (mxyan): Remove this when GRPCCall:useCronetWithEngine is removed
  if ([GRPCCall isUsingCronet]) {
    _channelFactory = [GRPCCronetChannelFactory factoryWithEngine:[GRPCCall cronetEngine]];
  }
  return [_channelFactory createChannelWithHost:_address channelArgs:[self channelArgs]];
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

// Flushes the host cache when connectivity status changes or when connection switch between Wifi
// and Cellular data, so that a new call will use a new channel. Otherwise, a new call will still
// use the cached channel which is no longer available and will cause gRPC to hang.
- (void)connectivityChange:(NSNotification *)note {
  [self disconnect];
}

@end

NS_ASSUME_NONNULL_END
