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
#import "GRPCConnectivityMonitor.h"
#import "GRPCCronetChannelFactory.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"
#import "version.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCallOptions.h>

@implementation GRPCChannel {
  GRPCChannelConfiguration *_configuration;
  grpc_channel *_unmanagedChannel;
}

- (grpc_call *)unmanagedCallWithPath:(NSString *)path
                     completionQueue:(nonnull GRPCCompletionQueue *)queue
                         callOptions:(GRPCCallOptions *)callOptions {
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
  grpc_call *call = grpc_channel_create_call(
      _unmanagedChannel, NULL, GRPC_PROPAGATE_DEFAULTS, queue.unmanagedQueue, path_slice,
      serverAuthority ? &host_slice : NULL, deadline_ms, NULL);
  if (serverAuthority) {
    grpc_slice_unref(host_slice);
  }
  grpc_slice_unref(path_slice);
  return call;
}

- (void)unmanagedCallUnref {
  [gChannelPool unrefChannelWithConfiguration:_configuration];
}

- (nullable instancetype)initWithUnmanagedChannel:(nullable grpc_channel *)unmanagedChannel
                                    configuration:(GRPCChannelConfiguration *)configuration {
  if ((self = [super init])) {
    _unmanagedChannel = unmanagedChannel;
    _configuration = configuration;
  }
  return self;
}

- (void)dealloc {
  grpc_channel_destroy(_unmanagedChannel);
}

+ (nullable instancetype)createChannelWithConfiguration:(GRPCChannelConfiguration *)config {
  NSString *host = config.host;
  if (host.length == 0) {
    return nil;
  }

  NSDictionary *channelArgs;
  if (config.callOptions.additionalChannelArgs.count != 0) {
    NSMutableDictionary *args = [config.channelArgs copy];
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
  return [gChannelPool channelWithConfiguration:channelConfig
                                  createChannel:^{
                                    return
                                        [GRPCChannel createChannelWithConfiguration:channelConfig];
                                  }];
}

+ (void)closeOpenConnections {
  [gChannelPool clear];
}

@end
