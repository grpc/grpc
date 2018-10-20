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

/** When all calls of a channel are destroyed, destroy the channel after this much seconds. */
NSTimeInterval kChannelDestroyDelay = 30;

/** Global instance of channel pool. */
static GRPCChannelPool *gChannelPool;

/**
 * Time the channel destroy when the channel's calls are unreffed. If there's new call, reset the
 * timer.
 */
@interface GRPCChannelRef : NSObject

- (instancetype)initWithDestroyDelay:(NSTimeInterval)destroyDelay
              destroyChannelCallback:(void (^)())destroyChannelCallback;

/** Add call ref count to the channel and maybe reset the timer. */
- (void)refChannel;

/** Reduce call ref count to the channel and maybe set the timer. */
- (void)unrefChannel;

/** Disconnect the channel immediately. */
- (void)disconnect;

@end

@implementation GRPCChannelRef {
  NSTimeInterval _destroyDelay;
  void (^_destroyChannelCallback)();

  NSUInteger _refCount;
  BOOL _disconnected;
  dispatch_queue_t _dispatchQueue;
  dispatch_queue_t _timerQueue;
  NSDate *_lastDispatch;
}

- (instancetype)initWithDestroyDelay:(NSTimeInterval)destroyDelay
              destroyChannelCallback:(void (^)())destroyChannelCallback {
  if ((self = [super init])) {
    _destroyDelay = destroyDelay;
    _destroyChannelCallback = destroyChannelCallback;

    _refCount = 1;
    _disconnected = NO;
    if (@available(iOS 8.0, *)) {
      _dispatchQueue = dispatch_queue_create(
          NULL,
          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, -1));
      _timerQueue =
          dispatch_queue_create(NULL, dispatch_queue_attr_make_with_qos_class(
                                          DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_DEFAULT, -1));
    } else {
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
      _timerQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_CONCURRENT);
    }
    _lastDispatch = nil;
  }
  return self;
}

- (void)refChannel {
  dispatch_async(_dispatchQueue, ^{
    if (!self->_disconnected) {
      self->_refCount++;
      self->_lastDispatch = nil;
    }
  });
}

- (void)unrefChannel {
  dispatch_async(_dispatchQueue, ^{
    if (!self->_disconnected) {
      self->_refCount--;
      if (self->_refCount == 0) {
        self->_lastDispatch = [NSDate date];
        dispatch_time_t delay =
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)kChannelDestroyDelay * 1e9);
        dispatch_after(delay, self->_timerQueue, ^{
          [self timerFire];
        });
      }
    }
  });
}

- (void)disconnect {
  dispatch_async(_dispatchQueue, ^{
    if (!self->_disconnected) {
      self->_lastDispatch = nil;
      self->_disconnected = YES;
      // Break retain loop
      self->_destroyChannelCallback = nil;
    }
  });
}

- (void)timerFire {
  dispatch_async(_dispatchQueue, ^{
    if (self->_disconnected || self->_lastDispatch == nil ||
        -[self->_lastDispatch timeIntervalSinceNow] < -kChannelDestroyDelay) {
      return;
    }
    self->_lastDispatch = nil;
    self->_disconnected = YES;
    self->_destroyChannelCallback();
    // Break retain loop
    self->_destroyChannelCallback = nil;
  });
}

@end

@implementation GRPCChannel {
  GRPCChannelConfiguration *_configuration;
  grpc_channel *_unmanagedChannel;
  GRPCChannelRef *_channelRef;
  dispatch_queue_t _dispatchQueue;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(path.length, @"path must not be empty.");
  NSAssert(queue, @"completionQueue must not be empty.");
  NSAssert(callOptions, @"callOptions must not be empty.");
  __block grpc_call *call = nil;
  dispatch_sync(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      NSString *serverAuthority =
          callOptions.transportType == GRPCTransportTypeCronet ? nil : callOptions.serverAuthority;
      NSTimeInterval timeout = callOptions.timeout;
      NSAssert(timeout >= 0, @"Invalid timeout");
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
    }
  });
  return call;
}

- (void)ref {
  dispatch_async(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      [self->_channelRef refChannel];
    }
  });
}

- (void)unref {
  dispatch_async(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      [self->_channelRef unrefChannel];
    }
  });
}

- (void)disconnect {
  dispatch_async(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      grpc_channel_destroy(self->_unmanagedChannel);
      self->_unmanagedChannel = nil;
      [self->_channelRef disconnect];
    }
  });
}

- (void)destroyChannel {
  dispatch_async(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      grpc_channel_destroy(self->_unmanagedChannel);
      self->_unmanagedChannel = nil;
      [gChannelPool removeChannel:self];
    }
  });
}

- (nullable instancetype)initWithUnmanagedChannel:(grpc_channel *_Nullable)unmanagedChannel
                                    configuration:(GRPCChannelConfiguration *)configuration {
  NSAssert(configuration, @"Configuration must not be empty.");
  if (!unmanagedChannel) {
    return nil;
  }
  if ((self = [super init])) {
    _unmanagedChannel = unmanagedChannel;
    _configuration = [configuration copy];
    _channelRef = [[GRPCChannelRef alloc] initWithDestroyDelay:kChannelDestroyDelay
                                        destroyChannelCallback:^{
                                          [self destroyChannel];
                                        }];
    if (@available(iOS 8.0, *)) {
      _dispatchQueue = dispatch_queue_create(
          NULL,
          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, -1));
    } else {
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }
  }
  return self;
}

- (void)dealloc {
  if (_unmanagedChannel) {
    grpc_channel_destroy(_unmanagedChannel);
  }
}

+ (nullable instancetype)createChannelWithConfiguration:(GRPCChannelConfiguration *)config {
  NSString *host = config.host;
  if (host.length == 0) {
    return nil;
  }

  NSDictionary *channelArgs;
  if (config.callOptions.additionalChannelArgs.count != 0) {
    NSMutableDictionary *args = [config.channelArgs mutableCopy];
    [args addEntriesFromDictionary:config.callOptions.additionalChannelArgs];
    channelArgs = args;
  } else {
    channelArgs = [config.channelArgs copy];
  }
  id<GRPCChannelFactory> factory = config.channelFactory;
  grpc_channel *unmanaged_channel = [factory createChannelWithHost:host channelArgs:channelArgs];
  return [[GRPCChannel alloc] initWithUnmanagedChannel:unmanaged_channel configuration:config];
}

+ (nullable instancetype)channelWithHost:(NSString *)host
                             callOptions:(GRPCCallOptions *)callOptions {
  static dispatch_once_t initChannelPool;
  dispatch_once(&initChannelPool, ^{
    gChannelPool = [[GRPCChannelPool alloc] init];
  });

  NSURL *hostURL = [NSURL URLWithString:[@"https://" stringByAppendingString:host]];
  if (hostURL.host && !hostURL.port) {
    host = [hostURL.host stringByAppendingString:@":443"];
  }

  GRPCChannelConfiguration *channelConfig =
      [[GRPCChannelConfiguration alloc] initWithHost:host callOptions:callOptions];

  return [gChannelPool channelWithConfiguration:channelConfig];
}

+ (void)closeOpenConnections {
  [gChannelPool removeAndCloseAllChannels];
}

@end
