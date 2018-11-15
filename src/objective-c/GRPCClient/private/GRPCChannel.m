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

#import "GRPCChannel.h"

#include <grpc/support/log.h>

#import "../internal/GRPCCallOptions+Internal.h"
#import "ChannelArgsUtil.h"
#import "GRPCChannelFactory.h"
#import "GRPCChannelPool.h"
#import "GRPCCompletionQueue.h"
#import "GRPCCronetChannelFactory.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "utilities.h"
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCallOptions.h>

/** When all calls of a channel are destroyed, destroy the channel after this much seconds. */
static const NSTimeInterval kDefaultChannelDestroyDelay = 30;

@implementation GRPCChannelConfiguration

- (nullable instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  GRPCAssert(host.length, NSInvalidArgumentException, @"Host must not be empty.");
  GRPCAssert(callOptions != nil, NSInvalidArgumentException, @"callOptions must not be empty.");
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
  if (userAgentPrefix.length != 0) {
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

  if (_callOptions.compressionAlgorithm != GRPC_COMPRESS_NONE) {
    args[@GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM] =
        [NSNumber numberWithInt:_callOptions.compressionAlgorithm];
  }

  if (_callOptions.keepaliveInterval != 0) {
    args[@GRPC_ARG_KEEPALIVE_TIME_MS] =
        [NSNumber numberWithUnsignedInteger:(NSUInteger)(_callOptions.keepaliveInterval * 1000)];
    args[@GRPC_ARG_KEEPALIVE_TIMEOUT_MS] =
        [NSNumber numberWithUnsignedInteger:(NSUInteger)(_callOptions.keepaliveTimeout * 1000)];
  }

  if (_callOptions.retryEnabled == NO) {
    args[@GRPC_ARG_ENABLE_RETRIES] = [NSNumber numberWithInt:_callOptions.retryEnabled];
  }

  if (_callOptions.connectMinTimeout > 0) {
    args[@GRPC_ARG_MIN_RECONNECT_BACKOFF_MS] =
        [NSNumber numberWithUnsignedInteger:(NSUInteger)(_callOptions.connectMinTimeout * 1000)];
  }
  if (_callOptions.connectInitialBackoff > 0) {
    args[@GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS] = [NSNumber
        numberWithUnsignedInteger:(NSUInteger)(_callOptions.connectInitialBackoff * 1000)];
  }
  if (_callOptions.connectMaxBackoff > 0) {
    args[@GRPC_ARG_MAX_RECONNECT_BACKOFF_MS] =
        [NSNumber numberWithUnsignedInteger:(NSUInteger)(_callOptions.connectMaxBackoff * 1000)];
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
  if (![object isKindOfClass:[GRPCChannelConfiguration class]]) {
    return NO;
  }
  GRPCChannelConfiguration *obj = (GRPCChannelConfiguration *)object;
  if (!(obj.host == _host || (_host != nil && [obj.host isEqualToString:_host]))) return NO;
  if (!(obj.callOptions == _callOptions || [obj.callOptions hasChannelOptionsEqualTo:_callOptions]))
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

@implementation GRPCChannel {
  GRPCChannelConfiguration *_configuration;

  dispatch_queue_t _dispatchQueue;
  grpc_channel *_unmanagedChannel;
  NSTimeInterval _destroyDelay;

  NSUInteger _refcount;
  NSDate *_lastDispatch;
}
@synthesize disconnected = _disconnected;

- (nullable instancetype)initWithChannelConfiguration:
    (GRPCChannelConfiguration *)channelConfiguration {
  return [self initWithChannelConfiguration:channelConfiguration
                               destroyDelay:kDefaultChannelDestroyDelay];
}

- (nullable instancetype)initWithChannelConfiguration:
                             (GRPCChannelConfiguration *)channelConfiguration
                                         destroyDelay:(NSTimeInterval)destroyDelay {
  GRPCAssert(channelConfiguration != nil, NSInvalidArgumentException,
             @"channelConfiguration must not be empty.");
  GRPCAssert(destroyDelay > 0, NSInvalidArgumentException, @"destroyDelay must be greater than 0.");
  if ((self = [super init])) {
    _configuration = [channelConfiguration copy];
    if (@available(iOS 8.0, *)) {
      _dispatchQueue = dispatch_queue_create(
          NULL,
          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, -1));
    } else {
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }

    // Create gRPC core channel object.
    NSString *host = channelConfiguration.host;
    GRPCAssert(host.length != 0, NSInvalidArgumentException, @"host cannot be nil");
    NSDictionary *channelArgs;
    if (channelConfiguration.callOptions.additionalChannelArgs.count != 0) {
      NSMutableDictionary *args = [channelConfiguration.channelArgs mutableCopy];
      [args addEntriesFromDictionary:channelConfiguration.callOptions.additionalChannelArgs];
      channelArgs = args;
    } else {
      channelArgs = channelConfiguration.channelArgs;
    }
    id<GRPCChannelFactory> factory = channelConfiguration.channelFactory;
    _unmanagedChannel = [factory createChannelWithHost:host channelArgs:channelArgs];
    if (_unmanagedChannel == NULL) {
      NSLog(@"Unable to create channel.");
      return nil;
    }
    _destroyDelay = destroyDelay;
    _disconnected = NO;
  }
  return self;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions
                        disconnected:(BOOL *)disconnected {
  GRPCAssert(path.length, NSInvalidArgumentException, @"path must not be empty.");
  GRPCAssert(queue, NSInvalidArgumentException, @"completionQueue must not be empty.");
  GRPCAssert(callOptions, NSInvalidArgumentException, @"callOptions must not be empty.");
  __block BOOL isDisconnected = NO;
  __block grpc_call *call = NULL;
  dispatch_sync(_dispatchQueue, ^{
    if (self->_disconnected) {
      isDisconnected = YES;
    } else {
      GRPCAssert(self->_unmanagedChannel != NULL, NSInternalInconsistencyException, @"Channel should have valid unmanaged channel.");

      NSString *serverAuthority =
          callOptions.transportType == GRPCTransportTypeCronet ? nil : callOptions.serverAuthority;
      NSTimeInterval timeout = callOptions.timeout;
      GRPCAssert(timeout >= 0, NSInvalidArgumentException, @"Invalid timeout");
      grpc_slice host_slice = grpc_empty_slice();
      if (serverAuthority) {
        host_slice = grpc_slice_from_copied_string(serverAuthority.UTF8String);
      }
      grpc_slice path_slice = grpc_slice_from_copied_string(path.UTF8String);
      gpr_timespec deadline_ms =
          timeout == 0
              ? gpr_inf_future(GPR_CLOCK_REALTIME)
              : gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                             gpr_time_from_millis((int64_t)(timeout * 1000), GPR_TIMESPAN));
      call = grpc_channel_create_call(self->_unmanagedChannel, NULL, GRPC_PROPAGATE_DEFAULTS,
                                      queue.unmanagedQueue, path_slice,
                                      serverAuthority ? &host_slice : NULL, deadline_ms, NULL);
      if (serverAuthority) {
        grpc_slice_unref(host_slice);
      }
      grpc_slice_unref(path_slice);
      if (call == NULL) {
        NSLog(@"Unable to create call.");
      } else {
        // Ref the channel;
        [self ref];
      }
    }
  });
  if (disconnected != nil) {
    *disconnected = isDisconnected;
  }
  return call;
}

// This function should be called on _dispatchQueue.
- (void)ref {
  dispatch_sync(_dispatchQueue, ^{
    self->_refcount++;
    if (self->_refcount == 1 && self->_lastDispatch != nil) {
      self->_lastDispatch = nil;
    }
  });
}

- (void)unref {
  dispatch_async(_dispatchQueue, ^{
    GRPCAssert(self->_refcount > 0, NSInternalInconsistencyException, @"Illegal reference count.");
    self->_refcount--;
    if (self->_refcount == 0 && !self->_disconnected) {
      // Start timer.
      dispatch_time_t delay =
          dispatch_time(DISPATCH_TIME_NOW, (int64_t)self->_destroyDelay * NSEC_PER_SEC);
      NSDate *now = [NSDate date];
      self->_lastDispatch = now;
      dispatch_after(delay, self->_dispatchQueue, ^{
        // Timed disconnection.
        if (!self->_disconnected && self->_lastDispatch == now) {
          grpc_channel_destroy(self->_unmanagedChannel);
          self->_unmanagedChannel = NULL;
          self->_disconnected = YES;
        }
      });
    }
  });
}

- (void)disconnect {
  dispatch_async(_dispatchQueue, ^{
    if (!self->_disconnected) {
      grpc_channel_destroy(self->_unmanagedChannel);
      self->_unmanagedChannel = nil;
      self->_disconnected = YES;
    }
  });
}

- (BOOL)disconnected {
  __block BOOL disconnected;
  dispatch_sync(_dispatchQueue, ^{
    disconnected = self->_disconnected;
  });
  return disconnected;
}

- (void)dealloc {
  if (_unmanagedChannel) {
    grpc_channel_destroy(_unmanagedChannel);
  }
}

@end
