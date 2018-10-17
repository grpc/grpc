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

#import "ChannelArgsUtil.h"
#import "GRPCChannel.h"

#ifdef GRPC_COMPILE_WITH_CRONET

#import <Cronet/Cronet.h>
#include <grpc/grpc_cronet.h>

NS_ASSUME_NONNULL_BEGIN

@implementation GRPCCronetChannelFactory {
  stream_engine *_cronetEngine;
}

+ (nullable instancetype)sharedInstance {
  static GRPCCronetChannelFactory *instance;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    instance = [[self alloc] initWithEngine:[Cronet getGlobalEngine]];
  });
  return instance;
}

- (nullable instancetype)initWithEngine:(stream_engine *)engine {
  if (!engine) {
    [NSException raise:NSInvalidArgumentException format:@"Cronet engine is NULL. Set it first."];
    return nil;
  }
  if ((self = [super init])) {
    _cronetEngine = engine;
  }
  return self;
}

- (nullable grpc_channel *)createChannelWithHost:(NSString *)host
                                     channelArgs:(nullable NSDictionary *)args {
  // Remove client authority filter since that is not supported
  args[@GRPC_ARG_DISABLE_CLIENT_AUTHORITY_FILTER] = [NSNumber numberWithInt:1];

  grpc_channel_args *channelArgs = BuildChannelArgs(args);
  grpc_channel *unmanagedChannel =
      grpc_cronet_secure_channel_create(_cronetEngine, host.UTF8String, channelArgs, NULL);
  FreeChannelArgs(channelArgs);
  return unmanagedChannel;
}

@end

NS_ASSUME_NONNULL_END

#else

NS_ASSUME_NONNULL_BEGIN

@implementation GRPCCronetChannelFactory

+ (nullable instancetype)sharedInstance {
  [NSException raise:NSInvalidArgumentException
              format:@"Must enable macro GRPC_COMPILE_WITH_CRONET to build Cronet channel."];
  return nil;
}

- (nullable grpc_channel *)createChannelWithHost:(NSString *)host
                                     channelArgs:(nullable NSDictionary *)args {
  [NSException raise:NSInvalidArgumentException
              format:@"Must enable macro GRPC_COMPILE_WITH_CRONET to build Cronet channel."];
  return nil;
}

@end

NS_ASSUME_NONNULL_END

#endif
