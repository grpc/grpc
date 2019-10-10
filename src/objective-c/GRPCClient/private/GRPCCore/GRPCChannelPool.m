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

#import "../../internal/GRPCCallOptions+Internal.h"
#import "GRPCChannel.h"
#import "GRPCChannelFactory.h"
#import "GRPCChannelPool+Test.h"
#import "GRPCChannelPool.h"
#import "GRPCCompletionQueue.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "GRPCWrappedCall.h"

#include <grpc/support/log.h>

extern const char *kCFStreamVarName;

static GRPCChannelPool *gChannelPool;
static dispatch_once_t gInitChannelPool;

/** When all calls of a channel are destroyed, destroy the channel after this much seconds. */
static const NSTimeInterval kDefaultChannelDestroyDelay = 30;

@implementation GRPCPooledChannel {
  GRPCChannelConfiguration *_channelConfiguration;
  NSTimeInterval _destroyDelay;

  NSHashTable<GRPCWrappedCall *> *_wrappedCalls;
  GRPCChannel *_wrappedChannel;
  NSDate *_lastTimedDestroy;
  dispatch_queue_t _timerQueue;
}

- (instancetype)initWithChannelConfiguration:(GRPCChannelConfiguration *)channelConfiguration {
  return [self initWithChannelConfiguration:channelConfiguration
                               destroyDelay:kDefaultChannelDestroyDelay];
}

- (nullable instancetype)initWithChannelConfiguration:
                             (GRPCChannelConfiguration *)channelConfiguration
                                         destroyDelay:(NSTimeInterval)destroyDelay {
  NSAssert(channelConfiguration != nil, @"channelConfiguration cannot be empty.");
  if (channelConfiguration == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _channelConfiguration = [channelConfiguration copy];
    _destroyDelay = destroyDelay;
    _wrappedCalls = [NSHashTable weakObjectsHashTable];
    _wrappedChannel = nil;
    _lastTimedDestroy = nil;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000 || __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    if (@available(iOS 8.0, macOS 10.10, *)) {
      _timerQueue = dispatch_queue_create(NULL, dispatch_queue_attr_make_with_qos_class(
                                                    DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, 0));
    } else {
#else
    {
#endif
      _timerQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }
  }

  return self;
}

- (void)dealloc {
  // Disconnect GRPCWrappedCall objects created but not yet removed
  if (_wrappedCalls.allObjects.count != 0) {
    for (GRPCWrappedCall *wrappedCall in _wrappedCalls.allObjects) {
      [wrappedCall channelDisconnected];
    };
  }
}

- (GRPCWrappedCall *)wrappedCallWithPath:(NSString *)path
                         completionQueue:(GRPCCompletionQueue *)queue
                             callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(path.length > 0, @"path must not be empty.");
  NSAssert(queue != nil, @"completionQueue must not be empty.");
  NSAssert(callOptions, @"callOptions must not be empty.");
  if (path.length == 0 || queue == nil || callOptions == nil) {
    return nil;
  }

  GRPCWrappedCall *call = nil;

  @synchronized(self) {
    if (_wrappedChannel == nil) {
      _wrappedChannel = [[GRPCChannel alloc] initWithChannelConfiguration:_channelConfiguration];
      if (_wrappedChannel == nil) {
        NSAssert(_wrappedChannel != nil, @"Unable to get a raw channel for proxy.");
        return nil;
      }
    }
    _lastTimedDestroy = nil;

    grpc_call *unmanagedCall =
        [_wrappedChannel unmanagedCallWithPath:path
                               completionQueue:[GRPCCompletionQueue completionQueue]
                                   callOptions:callOptions];
    if (unmanagedCall == NULL) {
      NSAssert(unmanagedCall != NULL, @"Unable to create grpc_call object");
      return nil;
    }

    call = [[GRPCWrappedCall alloc] initWithUnmanagedCall:unmanagedCall pooledChannel:self];
    if (call == nil) {
      NSAssert(call != nil, @"Unable to create GRPCWrappedCall object");
      grpc_call_unref(unmanagedCall);
      return nil;
    }

    [_wrappedCalls addObject:call];
  }
  return call;
}

- (void)notifyWrappedCallDealloc:(GRPCWrappedCall *)wrappedCall {
  NSAssert(wrappedCall != nil, @"wrappedCall cannot be empty.");
  if (wrappedCall == nil) {
    return;
  }
  @synchronized(self) {
    // Detect if all objects weakly referenced in _wrappedCalls are (implicitly) removed.
    // _wrappedCalls.count does not work here since the hash table may include deallocated weak
    // references. _wrappedCalls.allObjects forces removal of those objects.
    if (_wrappedCalls.allObjects.count == 0) {
      // No more call has reference to this channel. We may start the timer for destroying the
      // channel now.
      NSDate *now = [NSDate date];
      NSAssert(now != nil, @"Unable to create NSDate object 'now'.");
      _lastTimedDestroy = now;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)_destroyDelay * NSEC_PER_SEC),
                     _timerQueue, ^{
                       @synchronized(self) {
                         // Check _lastTimedDestroy against now in case more calls are created (and
                         // maybe destroyed) after this dispatch_async. In that case the current
                         // dispatch_after block should be discarded; the channel should be
                         // destroyed in a later dispatch_after block.
                         if (now != nil && self->_lastTimedDestroy == now) {
                           self->_wrappedChannel = nil;
                           self->_lastTimedDestroy = nil;
                         }
                       }
                     });
    }
  }
}

- (void)disconnect {
  NSArray<GRPCWrappedCall *> *copiedWrappedCalls = nil;
  @synchronized(self) {
    if (_wrappedChannel != nil) {
      _wrappedChannel = nil;
      copiedWrappedCalls = _wrappedCalls.allObjects;
      [_wrappedCalls removeAllObjects];
    }
  }
  for (GRPCWrappedCall *wrappedCall in copiedWrappedCalls) {
    [wrappedCall channelDisconnected];
  }
}

- (GRPCChannel *)wrappedChannel {
  GRPCChannel *channel = nil;
  @synchronized(self) {
    channel = _wrappedChannel;
  }
  return channel;
}

@end

@interface GRPCChannelPool ()

- (instancetype)initPrivate NS_DESIGNATED_INITIALIZER;

@end

@implementation GRPCChannelPool {
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCPooledChannel *> *_channelPool;
}

+ (instancetype)sharedInstance {
  dispatch_once(&gInitChannelPool, ^{
    gChannelPool = [[GRPCChannelPool alloc] initPrivate];
    NSAssert(gChannelPool != nil, @"Cannot initialize global channel pool.");
  });
  return gChannelPool;
}

- (instancetype)initPrivate {
  if ((self = [super init])) {
    _channelPool = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)dealloc {
}

- (GRPCPooledChannel *)channelWithHost:(NSString *)host callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(host.length > 0, @"Host must not be empty.");
  NSAssert(callOptions != nil, @"callOptions must not be empty.");
  if (host.length == 0 || callOptions == nil) {
    return nil;
  }

  // remove trailing slash of hostname
  NSURL *hostURL = [NSURL URLWithString:[@"https://" stringByAppendingString:host]];
  if (hostURL.host && hostURL.port == nil) {
    host = [hostURL.host stringByAppendingString:@":443"];
  }

  GRPCPooledChannel *pooledChannel = nil;
  GRPCChannelConfiguration *configuration =
      [[GRPCChannelConfiguration alloc] initWithHost:host callOptions:callOptions];
  @synchronized(self) {
    pooledChannel = _channelPool[configuration];
    if (pooledChannel == nil) {
      pooledChannel = [[GRPCPooledChannel alloc] initWithChannelConfiguration:configuration];
      _channelPool[configuration] = pooledChannel;
    }
  }
  return pooledChannel;
}

- (void)disconnectAllChannels {
  NSArray<GRPCPooledChannel *> *copiedPooledChannels;
  @synchronized(self) {
    copiedPooledChannels = _channelPool.allValues;
  }

  // Disconnect pooled channels.
  for (GRPCPooledChannel *pooledChannel in copiedPooledChannels) {
    [pooledChannel disconnect];
  }
}

@end

@implementation GRPCChannelPool (Test)

- (instancetype)initTestPool {
  return [self initPrivate];
}

@end
