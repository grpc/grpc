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
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#include <grpc/support/log.h>

extern const char *kCFStreamVarName;

static GRPCChannelPool *gChannelPool;
static dispatch_once_t gInitChannelPool;

@implementation GRPCChannelPool {
  NSMutableDictionary<GRPCChannelConfiguration *, GRPCChannel *> *_channelPool;
}

+ (nullable instancetype)sharedInstance {
  dispatch_once(&gInitChannelPool, ^{
    gChannelPool = [[GRPCChannelPool alloc] init];
    if (gChannelPool == nil) {
      [NSException raise:NSMallocException format:@"Cannot initialize global channel pool."];
    }
  });
  return gChannelPool;
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

- (GRPCChannel *)channelWithHost:(NSString *)host
                     callOptions:(GRPCCallOptions *)callOptions {
  return [self channelWithHost:host
                   callOptions:callOptions
                  destroyDelay:0];
}

- (GRPCChannel *)channelWithHost:(NSString *)host
                     callOptions:(GRPCCallOptions *)callOptions
                    destroyDelay:(NSTimeInterval)destroyDelay {
  NSAssert(host.length > 0, @"Host must not be empty.");
  NSAssert(callOptions != nil, @"callOptions must not be empty.");
  GRPCChannel *channel;
  GRPCChannelConfiguration *configuration =
  [[GRPCChannelConfiguration alloc] initWithHost:host callOptions:callOptions];
  @synchronized(self) {
    channel = _channelPool[configuration];
    if (channel == nil || channel.disconnected) {
      if (destroyDelay == 0) {
        channel = [[GRPCChannel alloc] initWithChannelConfiguration:configuration];
      } else {
        channel = [[GRPCChannel alloc] initWithChannelConfiguration:configuration destroyDelay:destroyDelay];
      }
      _channelPool[configuration] = channel;
    }
  }
  return channel;
}



+ (void)closeOpenConnections {
  [[GRPCChannelPool sharedInstance] destroyAllChannels];
}

- (void)destroyAllChannels {
  @synchronized(self) {
    for (id key in _channelPool) {
      [_channelPool[key] disconnect];
    }
    _channelPool = [NSMutableDictionary dictionary];
  }
}

- (void)connectivityChange:(NSNotification *)note {
  [self destroyAllChannels];
}

- (void)dealloc {
  [GRPCConnectivityMonitor unregisterObserver:self];
}

@end
