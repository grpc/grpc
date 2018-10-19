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

#import "GRPCChannel.h"
#import "GRPCChannelFactory.h"
#import "GRPCChannelPool.h"
#import "GRPCConnectivityMonitor.h"
#import "GRPCCronetChannelFactory.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#include <grpc/support/log.h>

extern const char *kCFStreamVarName;

@implementation GRPCChannelConfiguration

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  if ((self = [super init])) {
    _host = [host copy];
    _callOptions = [callOptions copy];
  }
  return self;
}

- (id<GRPCChannelFactory>)channelFactory {
  NSError *error;
  id<GRPCChannelFactory> factory;
  GRPCTransportType type = _callOptions.transportType;
  switch (type) {
    case GRPCTransportTypeChttp2BoringSSL:
      // TODO (mxyan): Remove when the API is deprecated
#ifdef GRPC_COMPILE_WITH_CRONET
      if (![GRPCCall isUsingCronet]) {
#endif
        factory = [GRPCSecureChannelFactory
            factoryWithPEMRootCertificates:_callOptions.PEMRootCertificates
                                privateKey:_callOptions.PEMPrivateKey
                                 certChain:_callOptions.PEMCertChain
                                     error:&error];
        if (factory == nil) {
          NSLog(@"Error creating secure channel factory: %@", error);
        }
        return factory;
#ifdef GRPC_COMPILE_WITH_CRONET
      }
#endif
      // fallthrough
    case GRPCTransportTypeCronet:
      return [GRPCCronetChannelFactory sharedInstance];
    case GRPCTransportTypeInsecure:
      return [GRPCInsecureChannelFactory sharedInstance];
  }
}

- (NSDictionary *)channelArgs {
  NSMutableDictionary *args = [NSMutableDictionary new];

  NSString *userAgent = @"grpc-objc/" GRPC_OBJC_VERSION_STRING;
  NSString *userAgentPrefix = _callOptions.userAgentPrefix;
  if (userAgentPrefix) {
    args[@GRPC_ARG_PRIMARY_USER_AGENT_STRING] =
        [_callOptions.userAgentPrefix stringByAppendingFormat:@" %@", userAgent];
  } else {
    args[@GRPC_ARG_PRIMARY_USER_AGENT_STRING] = userAgent;
  }

  NSString *hostNameOverride = _callOptions.hostNameOverride;
  if (hostNameOverride) {
    args[@GRPC_SSL_TARGET_NAME_OVERRIDE_ARG] = hostNameOverride;
  }

  if (_callOptions.responseSizeLimit) {
    args[@GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH] =
        [NSNumber numberWithUnsignedInteger:_callOptions.responseSizeLimit];
  }

  if (_callOptions.compressAlgorithm != GRPC_COMPRESS_NONE) {
    args[@GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM] =
        [NSNumber numberWithInt:_callOptions.compressAlgorithm];
  }

  if (_callOptions.keepaliveInterval != 0) {
    args[@GRPC_ARG_KEEPALIVE_TIME_MS] =
        [NSNumber numberWithUnsignedInteger:(unsigned int)(_callOptions.keepaliveInterval * 1000)];
    args[@GRPC_ARG_KEEPALIVE_TIMEOUT_MS] =
        [NSNumber numberWithUnsignedInteger:(unsigned int)(_callOptions.keepaliveTimeout * 1000)];
  }

  if (_callOptions.enableRetry == NO) {
    args[@GRPC_ARG_ENABLE_RETRIES] = [NSNumber numberWithInt:_callOptions.enableRetry];
  }

  if (_callOptions.connectMinTimeout > 0) {
    args[@GRPC_ARG_MIN_RECONNECT_BACKOFF_MS] =
        [NSNumber numberWithUnsignedInteger:(unsigned int)(_callOptions.connectMinTimeout * 1000)];
  }
  if (_callOptions.connectInitialBackoff > 0) {
    args[@GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS] = [NSNumber
        numberWithUnsignedInteger:(unsigned int)(_callOptions.connectInitialBackoff * 1000)];
  }
  if (_callOptions.connectMaxBackoff > 0) {
    args[@GRPC_ARG_MAX_RECONNECT_BACKOFF_MS] =
        [NSNumber numberWithUnsignedInteger:(unsigned int)(_callOptions.connectMaxBackoff * 1000)];
  }

  if (_callOptions.logContext != nil) {
    args[@GRPC_ARG_MOBILE_LOG_CONTEXT] = _callOptions.logContext;
  }

  if (_callOptions.channelPoolDomain.length != 0) {
    args[@GRPC_ARG_CHANNEL_POOL_DOMAIN] = _callOptions.channelPoolDomain;
  }

  [args addEntriesFromDictionary:_callOptions.additionalChannelArgs];

  return args;
}

- (nonnull id)copyWithZone:(nullable NSZone *)zone {
  GRPCChannelConfiguration *newConfig =
      [[GRPCChannelConfiguration alloc] initWithHost:_host callOptions:_callOptions];

  return newConfig;
}

- (BOOL)isEqual:(id)object {
  NSAssert([object isKindOfClass:[GRPCChannelConfiguration class]], @"Illegal :isEqual");
  GRPCChannelConfiguration *obj = (GRPCChannelConfiguration *)object;
  if (!(obj.host == _host || [obj.host isEqualToString:_host])) return NO;
  if (!(obj.callOptions == _callOptions || [obj.callOptions isChannelOptionsEqualTo:_callOptions]))
    return NO;

  return YES;
}

- (NSUInteger)hash {
  NSUInteger result = 0;
  result ^= _host.hash;
  result ^= _callOptions.channelOptionsHash;

  return result;
}

@end

#pragma mark GRPCChannelPool

@implementation GRPCChannelPool {
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannel *> *_channelPool;
}

- (instancetype)init {
  if ((self = [super init])) {
    _channelPool = [NSMutableDictionary dictionary];

    // Connectivity monitor is not required for CFStream
    char *enableCFStream = getenv(kCFStreamVarName);
    if (enableCFStream == nil || enableCFStream[0] != '1') {
      [GRPCConnectivityMonitor registerObserver:self selector:@selector(connectivityChange:)];
    }
  }
  return self;
}

- (void)dealloc {
  [GRPCConnectivityMonitor unregisterObserver:self];
}

- (GRPCChannel *)channelWithConfiguration:(GRPCChannelConfiguration *)configuration {
  __block GRPCChannel *channel;
  @synchronized(self) {
    if ([_channelPool objectForKey:configuration]) {
      channel = _channelPool[configuration];
      [channel unmanagedCallRef];
    } else {
      channel = [GRPCChannel createChannelWithConfiguration:configuration];
      if (channel != nil) {
        _channelPool[configuration] = channel;
      }
    }
  }
  return channel;
}

- (void)removeChannel:(GRPCChannel *)channel {
  @synchronized(self) {
    [_channelPool
        enumerateKeysAndObjectsUsingBlock:^(GRPCChannelConfiguration *_Nonnull key,
                                            GRPCChannel *_Nonnull obj, BOOL *_Nonnull stop) {
          if (obj == channel) {
            [self->_channelPool removeObjectForKey:key];
          }
        }];
  }
}

- (void)removeAllChannels {
  @synchronized(self) {
    _channelPool = [NSMutableDictionary dictionary];
  }
}

- (void)removeAndCloseAllChannels {
  @synchronized(self) {
    [_channelPool
        enumerateKeysAndObjectsUsingBlock:^(GRPCChannelConfiguration *_Nonnull key,
                                            GRPCChannel *_Nonnull obj, BOOL *_Nonnull stop) {
          [obj disconnect];
        }];
    _channelPool = [NSMutableDictionary dictionary];
  }
}

- (void)connectivityChange:(NSNotification *)note {
  [self removeAndCloseAllChannels];
}

@end
