/*
 *
 * Copyright 2019 gRPC authors.
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

#import "GRPCCoreCronetFactory.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCTransport.h>

#import "../GRPCCallInternal.h"
#import "../GRPCCoreFactory.h"
#import "GRPCCronetChannelFactory.h"

static GRPCCoreCronetFactory *gGRPCCoreCronetFactory = nil;
static dispatch_once_t gInitGRPCCoreCronetFactory;

@implementation GRPCCoreCronetFactory

+ (instancetype)sharedInstance {
  dispatch_once(&gInitGRPCCoreCronetFactory, ^{
    gGRPCCoreCronetFactory = [[GRPCCoreCronetFactory alloc] init];
  });
  return gGRPCCoreCronetFactory;
}

+ (void)load {
  [[GRPCTransportRegistry sharedInstance]
      registerTransportWithID:gGRPCCoreCronetID
                      factory:[GRPCCoreCronetFactory sharedInstance]];
}

- (GRPCTransport *)createTransportWithManager:(GRPCTransportManager *)transportManager {
  return [[GRPCCall2Internal alloc] initWithTransportManager:transportManager];
}

- (NSArray<id<GRPCInterceptorFactory>> *)transportInterceptorFactories {
  return nil;
}

- (id<GRPCChannelFactory>)createCoreChannelFactoryWithCallOptions:(GRPCCallOptions *)callOptions {
  return [GRPCCronetChannelFactory sharedInstance];
}

@end
