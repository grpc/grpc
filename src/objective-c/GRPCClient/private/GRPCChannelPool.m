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
        factory = [GRPCSecureChannelFactory factoryWithPEMRootCerts:_callOptions.PEMRootCertificates
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
  GRPCChannelConfiguration *newConfig = [[GRPCChannelConfiguration alloc] initWithHost:_host callOptions:_callOptions];

  return newConfig;
}

- (BOOL)isEqual:(id)object {
  NSAssert([object isKindOfClass:[GRPCChannelConfiguration class]], @"Illegal :isEqual");
  GRPCChannelConfiguration *obj = (GRPCChannelConfiguration *)object;
  if (!(obj.host == _host || [obj.host isEqualToString:_host])) return NO;
  if (!(obj.callOptions == _callOptions || [obj.callOptions isChannelOptionsEqualTo:_callOptions])) return NO;

  return YES;
}

- (NSUInteger)hash {
  NSUInteger result = 0;
  result ^= _host.hash;
  result ^= _callOptions.userAgentPrefix.hash;
  result ^= _callOptions.responseSizeLimit;
  result ^= _callOptions.compressAlgorithm;
  result ^= _callOptions.enableRetry;
  result ^= (unsigned int)(_callOptions.keepaliveInterval * 1000);
  result ^= (unsigned int)(_callOptions.keepaliveTimeout * 1000);
  result ^= (unsigned int)(_callOptions.connectMinTimeout * 1000);
  result ^= (unsigned int)(_callOptions.connectInitialBackoff * 1000);
  result ^= (unsigned int)(_callOptions.connectMaxBackoff * 1000);
  result ^= _callOptions.additionalChannelArgs.hash;
  result ^= _callOptions.PEMRootCertificates.hash;
  result ^= _callOptions.PEMPrivateKey.hash;
  result ^= _callOptions.PEMCertChain.hash;
  result ^= _callOptions.hostNameOverride.hash;
  result ^= _callOptions.transportType;
  result ^= [_callOptions.logContext hash];
  result ^= _callOptions.channelPoolDomain.hash;
  result ^= _callOptions.channelID;

  return result;
}

@end

#pragma mark GRPCChannelPool

@implementation GRPCChannelPool {
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannel *> *_channelPool;
  // Dedicated queue for timer
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)init {
  if ((self = [super init])) {
    _channelPool = [NSMutableDictionary dictionary];
    if (@available(iOS 8.0, *)) {
      _dispatchQueue = dispatch_queue_create(NULL, dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, -1));
    } else {
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }

    // Connectivity monitor is not required for CFStream
    char *enableCFStream = getenv(kCFStreamVarName);
    if (enableCFStream == nil || enableCFStream[0] != '1') {
      [GRPCConnectivityMonitor registerObserver:self selector:@selector(connectivityChange:)];
    }
  }
  return self;
}

- (void)dealloc {
  // Connectivity monitor is not required for CFStream
  char *enableCFStream = getenv(kCFStreamVarName);
  if (enableCFStream == nil || enableCFStream[0] != '1') {
    [GRPCConnectivityMonitor unregisterObserver:self];
  }
}

- (GRPCChannel *)channelWithConfiguration:(GRPCChannelConfiguration *)configuration {
  __block GRPCChannel *channel;
  dispatch_sync(_dispatchQueue, ^{
    if ([self->_channelPool objectForKey:configuration]) {
      channel = self->_channelPool[configuration];
      [channel unmanagedCallRef];
    } else {
      channel = [GRPCChannel createChannelWithConfiguration:configuration];
      self->_channelPool[configuration] = channel;
    }
  });
  return channel;
}

- (void)removeChannelWithConfiguration:(GRPCChannelConfiguration *)configuration {
  dispatch_async(_dispatchQueue, ^{
    [self->_channelPool removeObjectForKey:configuration];
  });
}

- (void)removeAllChannels {
  dispatch_sync(_dispatchQueue, ^{
    self->_channelPool = [NSMutableDictionary dictionary];
  });
}

- (void)removeAndCloseAllChannels {
  dispatch_sync(_dispatchQueue, ^{
    [self->_channelPool enumerateKeysAndObjectsUsingBlock:^(GRPCChannelConfiguration * _Nonnull key, GRPCChannel * _Nonnull obj, BOOL * _Nonnull stop) {
      [obj disconnect];
    }];
    self->_channelPool = [NSMutableDictionary dictionary];
  });
}

- (void)connectivityChange:(NSNotification *)note {
  [self removeAndCloseAllChannels];
}

@end
