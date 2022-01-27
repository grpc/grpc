/*
 *
 * Copyright 2018 gRPC authors.
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

#import "GRPCCronetChannelFactory.h"

#import "../ChannelArgsUtil.h"
#import "../GRPCChannel.h"

#import <Cronet/Cronet.h>
#include <grpc/grpc_cronet.h>

@implementation GRPCCronetChannelFactory {
  stream_engine *_cronetEngine;
}

+ (instancetype)sharedInstance {
  static GRPCCronetChannelFactory *instance;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    instance = [[self alloc] initWithEngine:[Cronet getGlobalEngine]];
  });
  return instance;
}

- (instancetype)initWithEngine:(stream_engine *)engine {
  NSAssert(engine != NULL, @"Cronet engine cannot be empty.");
  if (!engine) {
    return nil;
  }
  if ((self = [super init])) {
    _cronetEngine = engine;
  }
  return self;
}

- (grpc_channel *)createChannelWithHost:(NSString *)host channelArgs:(NSDictionary *)args {
  grpc_channel_args *channelArgs = GRPCBuildChannelArgs(args);
  grpc_channel *unmanagedChannel =
      grpc_cronet_secure_channel_create(_cronetEngine, host.UTF8String, channelArgs, NULL);
  GRPCFreeChannelArgs(channelArgs);
  return unmanagedChannel;
}

@end
