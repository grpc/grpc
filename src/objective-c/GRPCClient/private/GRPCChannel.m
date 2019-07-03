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
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCallOptions.h>

@implementation GRPCChannelConfiguration

- (instancetype)initWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(host.length > 0, @"Host must not be empty.");
  NSAssert(callOptions != nil, @"callOptions must not be empty.");
  if (host.length == 0 || callOptions == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _host = [host copy];
    _callOptions = [callOptions copy];
  }
  return self;
}

- (id<GRPCChannelFactory>)channelFactory {
  GRPCTransportType type = _callOptions.transportType;
  switch (type) {
    case GRPCTransportTypeChttp2BoringSSL:
      // TODO (mxyan): Remove when the API is deprecated
#ifdef GRPC_COMPILE_WITH_CRONET
      if (![GRPCCall isUsingCronet]) {
#else
    {
#endif
        NSError *error;
        id<GRPCChannelFactory> factory = [GRPCSecureChannelFactory
            factoryWithPEMRootCertificates:_callOptions.PEMRootCertificates
                                privateKey:_callOptions.PEMPrivateKey
                                 certChain:_callOptions.PEMCertificateChain
                                     error:&error];
        NSAssert(factory != nil, @"Failed to create secure channel factory");
        if (factory == nil) {
          NSLog(@"Error creating secure channel factory: %@", error);
        }
        return factory;
      }
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

  if (!_callOptions.retryEnabled) {
    args[@GRPC_ARG_ENABLE_RETRIES] = [NSNumber numberWithInt:_callOptions.retryEnabled ? 1 : 0];
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

- (id)copyWithZone:(NSZone *)zone {
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
  NSUInteger result = 31;
  result ^= _host.hash;
  result ^= _callOptions.channelOptionsHash;

  return result;
}

@end

@implementation GRPCChannel {
  GRPCChannelConfiguration *_configuration;

  grpc_channel *_unmanagedChannel;
}

- (instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)channelConfiguration {
  NSAssert(channelConfiguration != nil, @"channelConfiguration must not be empty.");
  if (channelConfiguration == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _configuration = [channelConfiguration copy];

    // Create gRPC core channel object.
    NSString *host = channelConfiguration.host;
    NSAssert(host.length != 0, @"host cannot be nil");
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
    NSAssert(_unmanagedChannel != NULL, @"Failed to create channel");
    if (_unmanagedChannel == NULL) {
      NSLog(@"Unable to create channel.");
      return nil;
    }
  }
  return self;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(path.length > 0, @"path must not be empty.");
  NSAssert(queue != nil, @"completionQueue must not be empty.");
  NSAssert(callOptions != nil, @"callOptions must not be empty.");
  if (path.length == 0) return NULL;
  if (queue == nil) return NULL;
  if (callOptions == nil) return NULL;

  grpc_call *call = NULL;
  // No need to lock here since _unmanagedChannel is only changed in _dealloc
  NSAssert(_unmanagedChannel != NULL, @"Channel should have valid unmanaged channel.");
  if (_unmanagedChannel == NULL) return NULL;

  NSString *serverAuthority =
      callOptions.transportType == GRPCTransportTypeCronet ? nil : callOptions.serverAuthority;
  NSTimeInterval timeout = callOptions.timeout;
  NSAssert(timeout >= 0, @"Invalid timeout");
  if (timeout < 0) return NULL;
  grpc_slice host_slice = serverAuthority
                              ? grpc_slice_from_copied_string(serverAuthority.UTF8String)
                              : grpc_empty_slice();
  grpc_slice path_slice = grpc_slice_from_copied_string(path.UTF8String);
  gpr_timespec deadline_ms =
      timeout == 0 ? gpr_inf_future(GPR_CLOCK_REALTIME)
                   : gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                  gpr_time_from_millis((int64_t)(timeout * 1000), GPR_TIMESPAN));
  call = grpc_channel_create_call(_unmanagedChannel, NULL, GRPC_PROPAGATE_DEFAULTS,
                                  queue.unmanagedQueue, path_slice,
                                  serverAuthority ? &host_slice : NULL, deadline_ms, NULL);
  if (serverAuthority) {
    grpc_slice_unref(host_slice);
  }
  grpc_slice_unref(path_slice);
  NSAssert(call != nil, @"Unable to create call.");
  if (call == NULL) {
    NSLog(@"Unable to create call.");
  }
  return call;
}

- (void)dealloc {
  if (_unmanagedChannel) {
    grpc_channel_destroy(_unmanagedChannel);
  }
}

@end
