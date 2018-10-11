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

// When all calls of a channel are destroyed, destroy the channel after this much seconds.
const NSTimeInterval kChannelDestroyDelay = 30;

@implementation GRPCChannelConfiguration

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  if ((self = [super init])) {
    _host = host;
    _callOptions = callOptions;
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
        if (error) {
          NSLog(@"Error creating secure channel factory: %@", error);
          return nil;
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
    default:
      GPR_UNREACHABLE_CODE(return nil);
  }
}

- (NSMutableDictionary *)channelArgs {
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
  GRPCChannelConfiguration *newConfig = [[GRPCChannelConfiguration alloc] init];
  newConfig.host = _host;
  newConfig.callOptions = _callOptions;

  return newConfig;
}

- (BOOL)isEqual:(id)object {
  NSAssert([object isKindOfClass:[GRPCChannelConfiguration class]], @"Illegal :isEqual");
  GRPCChannelConfiguration *obj = (GRPCChannelConfiguration *)object;
  if (!(obj.host == _host || [obj.host isEqualToString:_host])) return NO;
  if (!(obj.callOptions.userAgentPrefix == _callOptions.userAgentPrefix ||
        [obj.callOptions.userAgentPrefix isEqualToString:_callOptions.userAgentPrefix]))
    return NO;
  if (!(obj.callOptions.responseSizeLimit == _callOptions.responseSizeLimit)) return NO;
  if (!(obj.callOptions.compressAlgorithm == _callOptions.compressAlgorithm)) return NO;
  if (!(obj.callOptions.enableRetry == _callOptions.enableRetry)) return NO;
  if (!(obj.callOptions.keepaliveInterval == _callOptions.keepaliveInterval)) return NO;
  if (!(obj.callOptions.keepaliveTimeout == _callOptions.keepaliveTimeout)) return NO;
  if (!(obj.callOptions.connectMinTimeout == _callOptions.connectMinTimeout)) return NO;
  if (!(obj.callOptions.connectInitialBackoff == _callOptions.connectInitialBackoff)) return NO;
  if (!(obj.callOptions.connectMaxBackoff == _callOptions.connectMaxBackoff)) return NO;
  if (!(obj.callOptions.additionalChannelArgs == _callOptions.additionalChannelArgs ||
        [obj.callOptions.additionalChannelArgs
            isEqualToDictionary:_callOptions.additionalChannelArgs]))
    return NO;
  if (!(obj.callOptions.PEMRootCertificates == _callOptions.PEMRootCertificates ||
        [obj.callOptions.PEMRootCertificates isEqualToString:_callOptions.PEMRootCertificates]))
    return NO;
  if (!(obj.callOptions.PEMPrivateKey == _callOptions.PEMPrivateKey ||
        [obj.callOptions.PEMPrivateKey isEqualToString:_callOptions.PEMPrivateKey]))
    return NO;
  if (!(obj.callOptions.PEMCertChain == _callOptions.PEMCertChain ||
        [obj.callOptions.PEMCertChain isEqualToString:_callOptions.PEMCertChain]))
    return NO;
  if (!(obj.callOptions.hostNameOverride == _callOptions.hostNameOverride ||
        [obj.callOptions.hostNameOverride isEqualToString:_callOptions.hostNameOverride]))
    return NO;
  if (!(obj.callOptions.transportType == _callOptions.transportType)) return NO;
  if (!(obj.callOptions.logContext == _callOptions.logContext ||
        [obj.callOptions.logContext isEqual:_callOptions.logContext]))
    return NO;
  if (!(obj.callOptions.channelPoolDomain == _callOptions.channelPoolDomain ||
        [obj.callOptions.channelPoolDomain isEqualToString:_callOptions.channelPoolDomain]))
    return NO;
  if (!(obj.callOptions.channelID == _callOptions.channelID)) return NO;

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

/**
 * Time the channel destroy when the channel's calls are unreffed. If there's new call, reset the
 * timer.
 */
@interface GRPCChannelCallRef : NSObject

- (instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)configuration
                                destroyDelay:(NSTimeInterval)destroyDelay
                               dispatchQueue:(dispatch_queue_t)dispatchQueue
                              destroyChannel:(void (^)())destroyChannel;

/** Add call ref count to the channel and maybe reset the timer. */
- (void)refChannel;

/** Reduce call ref count to the channel and maybe set the timer. */
- (void)unrefChannel;

@end

@implementation GRPCChannelCallRef {
  GRPCChannelConfiguration *_configuration;
  NSTimeInterval _destroyDelay;
  // We use dispatch queue for this purpose since timer invalidation must happen on the same
  // thread which issued the timer.
  dispatch_queue_t _dispatchQueue;
  void (^_destroyChannel)();

  NSUInteger _refCount;
  NSTimer *_timer;
}

- (instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)configuration
                                destroyDelay:(NSTimeInterval)destroyDelay
                               dispatchQueue:(dispatch_queue_t)dispatchQueue
                              destroyChannel:(void (^)())destroyChannel {
  if ((self = [super init])) {
    _configuration = configuration;
    _destroyDelay = destroyDelay;
    _dispatchQueue = dispatchQueue;
    _destroyChannel = destroyChannel;

    _refCount = 0;
    _timer = nil;
  }
  return self;
}

// This function is protected by channel pool dispatch queue.
- (void)refChannel {
  _refCount++;
  if (_timer) {
    [_timer invalidate];
  }
  _timer = nil;
}

// This function is protected by channel spool dispatch queue.
- (void)unrefChannel {
  self->_refCount--;
  if (self->_refCount == 0) {
    if (self->_timer) {
      [self->_timer invalidate];
    }
    self->_timer = [NSTimer scheduledTimerWithTimeInterval:self->_destroyDelay
                                                    target:self
                                                  selector:@selector(timerFire:)
                                                  userInfo:nil
                                                   repeats:NO];
  }
}

- (void)timerFire:(NSTimer *)timer {
  dispatch_sync(_dispatchQueue, ^{
    if (self->_timer == nil || self->_timer != timer) {
      return;
    }
    self->_timer = nil;
    self->_destroyChannel(self->_configuration);
  });
}

@end

#pragma mark GRPCChannelPool

@implementation GRPCChannelPool {
  NSTimeInterval _channelDestroyDelay;
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannel *> *_channelPool;
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannelCallRef *> *_callRefs;
  // Dedicated queue for timer
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)init {
  return [self initWithChannelDestroyDelay:kChannelDestroyDelay];
}

- (instancetype)initWithChannelDestroyDelay:(NSTimeInterval)channelDestroyDelay {
  if ((self = [super init])) {
    _channelDestroyDelay = channelDestroyDelay;
    _channelPool = [NSMutableDictionary dictionary];
    _callRefs = [NSMutableDictionary dictionary];
    _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);

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

- (GRPCChannel *)channelWithConfiguration:(GRPCChannelConfiguration *)configuration
                            createChannel:(GRPCChannel * (^)(void))createChannel {
  __block GRPCChannel *channel;
  dispatch_sync(_dispatchQueue, ^{
    if ([self->_channelPool objectForKey:configuration]) {
      [self->_callRefs[configuration] refChannel];
      channel = self->_channelPool[configuration];
    } else {
      channel = createChannel();
      self->_channelPool[configuration] = channel;

      GRPCChannelCallRef *callRef = [[GRPCChannelCallRef alloc]
          initWithChannelConfiguration:configuration
                          destroyDelay:self->_channelDestroyDelay
                         dispatchQueue:self->_dispatchQueue
                        destroyChannel:^(GRPCChannelConfiguration *configuration) {
                          [self->_channelPool removeObjectForKey:configuration];
                          [self->_callRefs removeObjectForKey:configuration];
                        }];
      [callRef refChannel];
      self->_callRefs[configuration] = callRef;
    }
  });
  return channel;
}

- (void)unrefChannelWithConfiguration:configuration {
  dispatch_sync(_dispatchQueue, ^{
    if ([self->_channelPool objectForKey:configuration]) {
      [self->_callRefs[configuration] unrefChannel];
    }
  });
}

- (void)clear {
  dispatch_sync(_dispatchQueue, ^{
    self->_channelPool = [NSMutableDictionary dictionary];
    self->_callRefs = [NSMutableDictionary dictionary];
  });
}

- (void)connectivityChange:(NSNotification *)note {
  [self clear];
}

@end
