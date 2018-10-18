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

// When all calls of a channel are destroyed, destroy the channel after this much seconds.
NSTimeInterval kChannelDestroyDelay = 30;

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
  // We use dispatch queue for this purpose since timer invalidation must happen on the same
  // thread which issued the timer.
  dispatch_queue_t _dispatchQueue;
  void (^_destroyChannelCallback)();

  NSUInteger _refCount;
  NSTimer *_timer;
  BOOL _disconnected;
}

- (instancetype)initWithDestroyDelay:(NSTimeInterval)destroyDelay
              destroyChannelCallback:(void (^)())destroyChannelCallback {
  if ((self = [super init])) {
    _destroyDelay = destroyDelay;
    _destroyChannelCallback = destroyChannelCallback;

    _refCount = 1;
    _timer = nil;
    _disconnected = NO;
  }
  return self;
}

// This function is protected by channel dispatch queue.
- (void)refChannel {
  if (!_disconnected) {
    _refCount++;
    [_timer invalidate];
    _timer = nil;
  }
}

// This function is protected by channel dispatch queue.
- (void)unrefChannel {
  if (!_disconnected) {
    _refCount--;
    if (_refCount == 0) {
      [_timer invalidate];
      _timer = [NSTimer scheduledTimerWithTimeInterval:self->_destroyDelay
                                                target:self
                                              selector:@selector(timerFire:)
                                              userInfo:nil
                                               repeats:NO];
    }
  }
}

// This function is protected by channel dispatch queue.
- (void)disconnect {
  if (!_disconnected) {
    [_timer invalidate];
    _timer = nil;
    _disconnected = YES;
    // Break retain loop
    _destroyChannelCallback = nil;
  }
}

// This function is protected by channel dispatch queue.
- (void)timerFire:(NSTimer *)timer {
  if (_disconnected || _timer == nil || _timer != timer) {
    return;
  }
  _timer = nil;
  _destroyChannelCallback();
  // Break retain loop
  _destroyChannelCallback = nil;
  _disconnected = YES;
}

@end

@implementation GRPCChannel {
  GRPCChannelConfiguration *_configuration;
  grpc_channel *_unmanagedChannel;
  GRPCChannelRef *_channelRef;
  dispatch_queue_t _dispatchQueue;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(nonnull GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions {
  __block grpc_call *call = nil;
  dispatch_sync(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      NSString *serverAuthority = callOptions.serverAuthority;
      NSTimeInterval timeout = callOptions.timeout;
      GPR_ASSERT(timeout >= 0);
      grpc_slice host_slice = grpc_empty_slice();
      if (serverAuthority) {
        host_slice = grpc_slice_from_copied_string(serverAuthority.UTF8String);
      }
      grpc_slice path_slice = grpc_slice_from_copied_string(path.UTF8String);
      gpr_timespec deadline_ms =
      timeout == 0 ? gpr_inf_future(GPR_CLOCK_REALTIME)
      : gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                     gpr_time_from_millis((int64_t)(timeout * 1000), GPR_TIMESPAN));
      call = grpc_channel_create_call(
                                      self->_unmanagedChannel, NULL, GRPC_PROPAGATE_DEFAULTS, queue.unmanagedQueue, path_slice,
                                      serverAuthority ? &host_slice : NULL, deadline_ms, NULL);
      if (serverAuthority) {
        grpc_slice_unref(host_slice);
      }
      grpc_slice_unref(path_slice);
    }
  });
  return call;
}

- (void)unmanagedCallRef {
  dispatch_async(_dispatchQueue, ^{
    if (self->_unmanagedChannel) {
      [self->_channelRef refChannel];
    }
  });
}

- (void)unmanagedCallUnref {
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
      [gChannelPool removeChannelWithConfiguration:self->_configuration];
    }
  });
}

- (nullable instancetype)initWithUnmanagedChannel:(nullable grpc_channel *)unmanagedChannel
                                    configuration:(GRPCChannelConfiguration *)configuration {
  if ((self = [super init])) {
    _unmanagedChannel = unmanagedChannel;
    _configuration = configuration;
    _channelRef = [[GRPCChannelRef alloc] initWithDestroyDelay:kChannelDestroyDelay destroyChannelCallback:^{
      [self destroyChannel];
    }];
    if (@available(iOS 8.0, *)) {
      _dispatchQueue = dispatch_queue_create(NULL, dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, -1));
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
    channelArgs = config.channelArgs;
  }
  id<GRPCChannelFactory> factory = config.channelFactory;
  grpc_channel *unmanaged_channel = [factory createChannelWithHost:host channelArgs:channelArgs];
  return [[GRPCChannel alloc] initWithUnmanagedChannel:unmanaged_channel configuration:config];
}

static dispatch_once_t initChannelPool;
static GRPCChannelPool *gChannelPool;

+ (nullable instancetype)channelWithHost:(NSString *)host
                             callOptions:(GRPCCallOptions *)callOptions {
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
