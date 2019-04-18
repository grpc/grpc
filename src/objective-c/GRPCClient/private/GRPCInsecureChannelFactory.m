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

#import "GRPCInsecureChannelFactory.h"

#import "ChannelArgsUtil.h"
#import "GRPCChannel.h"

@implementation GRPCInsecureChannelFactory

+ (instancetype)sharedInstance {
  static GRPCInsecureChannelFactory *instance;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    instance = [[self alloc] init];
  });
  return instance;
}

- (grpc_channel *)createChannelWithHost:(NSString *)host channelArgs:(NSDictionary *)args {
  grpc_channel_args *coreChannelArgs = GRPCBuildChannelArgs(args);
  grpc_channel *unmanagedChannel =
      grpc_insecure_channel_create(host.UTF8String, coreChannelArgs, NULL);
  GRPCFreeChannelArgs(coreChannelArgs);
  return unmanagedChannel;
}

@end
