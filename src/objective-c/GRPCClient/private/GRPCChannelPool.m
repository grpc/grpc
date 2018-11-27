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

#import "../internal/GRPCCallOptions+Internal.h"
#import "GRPCChannel.h"
#import "GRPCChannelFactory.h"
#import "GRPCChannelPool.h"
#import "GRPCConnectivityMonitor.h"
#import "GRPCCronetChannelFactory.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "utilities.h"
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#include <grpc/support/log.h>

extern const char *kCFStreamVarName;

static GRPCChannelPool *gChannelPool;
static dispatch_once_t gInitChannelPool;

/** When all calls of a channel are destroyed, destroy the channel after this much seconds. */
static const NSTimeInterval kDefaultChannelDestroyDelay = 30;

@interface GRPCChannelPool ()

- (GRPCChannel *)refChannelWithConfiguration:(GRPCChannelConfiguration *)configuration;

- (void)unrefChannelWithConfiguration:(GRPCChannelConfiguration *)configuration;

@end

@implementation GRPCPooledChannel {
  __weak GRPCChannelPool *_channelPool;
  GRPCChannelConfiguration *_channelConfiguration;
  NSMutableSet *_unmanagedCalls;
  GRPCChannel *_wrappedChannel;
}

- (instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)channelConfiguration
                                 channelPool:(GRPCChannelPool *)channelPool {
  NSAssert(channelConfiguration != nil, @"channelConfiguration cannot be empty.");
  NSAssert(channelPool != nil, @"channelPool cannot be empty.");
  if (channelPool == nil || channelConfiguration == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _channelPool = channelPool;
    _channelConfiguration = channelConfiguration;
    _unmanagedCalls = [NSMutableSet set];
    _wrappedChannel = nil;
  }

  return self;
}

- (void)dealloc {
  NSAssert([_unmanagedCalls count] == 0 && _wrappedChannel == nil, @"Pooled channel should only be"
           "destroyed after the wrapped channel is destroyed");
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(path.length > 0, @"path must not be empty.");
  NSAssert(queue != nil, @"completionQueue must not be empty.");
  NSAssert(callOptions, @"callOptions must not be empty.");
  if (path.length == 0 || queue == nil || callOptions == nil) return NULL;

  grpc_call *call = NULL;
  @synchronized(self) {
    if (_wrappedChannel == nil) {
      __strong GRPCChannelPool *strongPool = _channelPool;
      if (strongPool) {
        _wrappedChannel = [strongPool refChannelWithConfiguration:_channelConfiguration];
      }
      NSAssert(_wrappedChannel != nil, @"Unable to get a raw channel for proxy.");
    }
    call =
        [_wrappedChannel unmanagedCallWithPath:path completionQueue:queue callOptions:callOptions];
    if (call != NULL) {
      [_unmanagedCalls addObject:[NSValue valueWithPointer:call]];
    }
  }
  return call;
}

- (void)destroyUnmanagedCall:(grpc_call *)unmanagedCall {
  if (unmanagedCall == NULL) {
    return;
  }

  grpc_call_unref(unmanagedCall);
  @synchronized(self) {
    NSValue *removedCall = [NSValue valueWithPointer:unmanagedCall];
    [_unmanagedCalls removeObject:removedCall];
    if ([_unmanagedCalls count] == 0) {
      _wrappedChannel = nil;
      GRPCChannelPool *strongPool = _channelPool;
      [strongPool unrefChannelWithConfiguration:_channelConfiguration];
    }
  }
}

- (void)disconnect {
  @synchronized(self) {
    if (_wrappedChannel != nil) {
      _wrappedChannel = nil;
      [_unmanagedCalls removeAllObjects];
      GRPCChannelPool *strongPool = _channelPool;
      [strongPool unrefChannelWithConfiguration:_channelConfiguration];
    }
  }
}

@end

@implementation GRPCPooledChannel (Test)

- (GRPCChannel *)wrappedChannel {
  GRPCChannel *channel = nil;
  @synchronized(self) {
    channel = _wrappedChannel;
  }
  return channel;
}

@end

/**
 * A convenience value type for cached channel.
 */
@interface GRPCChannelRecord : NSObject

/** Pointer to the raw channel. May be nil when the channel has been destroyed. */
@property GRPCChannel *channel;

/** Channel proxy corresponding to this channel configuration. */
@property GRPCPooledChannel *pooledChannel;

/** Last time when a timed destroy is initiated on the channel. */
@property NSDate *timedDestroyDate;

/** Reference count of the proxy to the channel. */
@property NSUInteger refCount;

@end

@implementation GRPCChannelRecord

@end

@interface GRPCChannelPool ()

- (instancetype)initInstanceWithDestroyDelay:(NSTimeInterval)destroyDelay NS_DESIGNATED_INITIALIZER;

@end

@implementation GRPCChannelPool {
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannelRecord *> *_channelPool;
  dispatch_queue_t _dispatchQueue;
  NSTimeInterval _destroyDelay;
}

+ (instancetype)sharedInstance {
  dispatch_once(&gInitChannelPool, ^{
    gChannelPool = [[GRPCChannelPool alloc] initInstanceWithDestroyDelay:kDefaultChannelDestroyDelay];
    NSAssert(gChannelPool != nil, @"Cannot initialize global channel pool.");
  });
  return gChannelPool;
}

- (instancetype)initInstanceWithDestroyDelay:(NSTimeInterval)destroyDelay {
  if ((self = [super init])) {
    _channelPool = [NSMutableDictionary dictionary];
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000 || __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    if (@available(iOS 8.0, macOS 10.10, *)) {
      _dispatchQueue = dispatch_queue_create(
          NULL,
          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, 0));
    } else {
#else
    {
#endif
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }
    _destroyDelay = destroyDelay;

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

- (GRPCPooledChannel *)channelWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(host.length > 0, @"Host must not be empty.");
  NSAssert(callOptions != nil, @"callOptions must not be empty.");
  if (host.length == 0 || callOptions == nil) {
    return nil;
  }

  GRPCPooledChannel *pooledChannel = nil;
  GRPCChannelConfiguration *configuration =
      [[GRPCChannelConfiguration alloc] initWithHost:host callOptions:callOptions];
  @synchronized(self) {
    GRPCChannelRecord *record = _channelPool[configuration];
    if (record == nil) {
      record = [[GRPCChannelRecord alloc] init];
      record.pooledChannel =
          [[GRPCPooledChannel alloc] initWithChannelConfiguration:configuration channelPool:self];
      _channelPool[configuration] = record;
      pooledChannel = record.pooledChannel;
    } else {
      pooledChannel = record.pooledChannel;
    }
  }
  return pooledChannel;
}

- (GRPCChannel *)refChannelWithConfiguration:(GRPCChannelConfiguration *)configuration {
  GRPCChannel *ret = nil;
  @synchronized(self) {
    NSAssert(configuration != nil, @"configuration cannot be empty.");
    if (configuration == nil) {
      return nil;
    }

    GRPCChannelRecord *record = _channelPool[configuration];
    NSAssert(record != nil, @"No record corresponding to a proxy.");
    if (record == nil) {
      return nil;
    }

    record.refCount++;
    record.timedDestroyDate = nil;
    if (record.channel == nil) {
      // Channel is already destroyed;
      record.channel = [[GRPCChannel alloc] initWithChannelConfiguration:configuration];
    }
    ret = record.channel;
  }
  return ret;
}

- (void)unrefChannelWithConfiguration:(GRPCChannelConfiguration *)configuration {
  @synchronized(self) {
    GRPCChannelRecord *record = _channelPool[configuration];
    NSAssert(record != nil, @"No record corresponding to a proxy.");
    if (record == nil) {
      return;
    }
    NSAssert(record.refCount > 0, @"Inconsistent channel refcount.");
    if (record.refCount > 0) {
      record.refCount--;
      if (record.refCount == 0) {
        NSDate *now = [NSDate date];
        record.timedDestroyDate = now;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(_destroyDelay * NSEC_PER_SEC)),
                       _dispatchQueue, ^{
                         @synchronized(self) {
                           if (now == record.timedDestroyDate) {
                             // Destroy the raw channel and reset related records.
                             record.timedDestroyDate = nil;
                             record.channel = nil;
                           }
                         }
                       });
      }
    }
  }
}

- (void)disconnectAllChannels {
  NSMutableSet<GRPCPooledChannel *> *proxySet = [NSMutableSet set];
  @synchronized(self) {
    [_channelPool
        enumerateKeysAndObjectsUsingBlock:^(GRPCChannelConfiguration *_Nonnull key,
                                            GRPCChannelRecord *_Nonnull obj, BOOL *_Nonnull stop) {
          obj.channel = nil;
          obj.timedDestroyDate = nil;
          [proxySet addObject:obj.pooledChannel];
        }];
  }
  // Disconnect proxies
  [proxySet enumerateObjectsUsingBlock:^(GRPCPooledChannel *_Nonnull obj, BOOL *_Nonnull stop) {
    [obj disconnect];
  }];
}

- (void)connectivityChange:(NSNotification *)note {
  [self disconnectAllChannels];
}

@end

@implementation GRPCChannelPool (Test)

- (instancetype)initTestPoolWithDestroyDelay:(NSTimeInterval)destroyDelay {
  return [self initInstanceWithDestroyDelay:destroyDelay];
}

@end
