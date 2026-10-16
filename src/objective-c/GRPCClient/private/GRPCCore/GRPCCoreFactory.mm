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

#import "GRPCCoreFactory.h"

#import <GRPCClient/GRPCTransport.h>

#import "GRPCCallInternal.h"
#import "GRPCInsecureChannelFactory.h"
#import "GRPCSecureChannelFactory.h"

static GRPCCoreSecureFactory *gGRPCCoreSecureFactory = nil;
static GRPCCoreInsecureFactory *gGRPCCoreInsecureFactory = nil;
static dispatch_once_t gInitGRPCCoreSecureFactory;
static dispatch_once_t gInitGRPCCoreInsecureFactory;

@implementation GRPCCoreSecureFactory

+ (instancetype)sharedInstance {
  dispatch_once(&gInitGRPCCoreSecureFactory, ^{
    gGRPCCoreSecureFactory = [[GRPCCoreSecureFactory alloc] init];
  });
  return gGRPCCoreSecureFactory;
}

+ (void)load {
  [[GRPCTransportRegistry sharedInstance]
      registerTransportWithID:GRPCDefaultTransportImplList.core_secure
                      factory:[self sharedInstance]];
}

- (GRPCTransport *)createTransportWithManager:(GRPCTransportManager *)transportManager {
  return [[GRPCCall2Internal alloc] initWithTransportManager:transportManager];
}

- (NSArray<id<GRPCInterceptorFactory>> *)transportInterceptorFactories {
  return nil;
}

- (id<GRPCChannelFactory>)createCoreChannelFactoryWithCallOptions:(GRPCCallOptions *)callOptions {
  NSError *error;
  id<GRPCChannelFactory> factory =
      [GRPCSecureChannelFactory factoryWithPEMRootCertificates:callOptions.PEMRootCertificates
                                                    privateKey:callOptions.PEMPrivateKey
                                                     certChain:callOptions.PEMCertificateChain
                                                         error:&error];
  if (error != nil) {
    NSLog(@"Unable to create secure channel factory");
    return nil;
  }
  return factory;
}

@end

@implementation GRPCCoreInsecureFactory

+ (instancetype)sharedInstance {
  dispatch_once(&gInitGRPCCoreInsecureFactory, ^{
    gGRPCCoreInsecureFactory = [[GRPCCoreInsecureFactory alloc] init];
  });
  return gGRPCCoreInsecureFactory;
}

+ (void)load {
  [[GRPCTransportRegistry sharedInstance]
      registerTransportWithID:GRPCDefaultTransportImplList.core_insecure
                      factory:[self sharedInstance]];
}

- (GRPCTransport *)createTransportWithManager:(GRPCTransportManager *)transportManager {
  return [[GRPCCall2Internal alloc] initWithTransportManager:transportManager];
}

- (NSArray<id<GRPCInterceptorFactory>> *)transportInterceptorFactories {
  return nil;
}

- (id<GRPCChannelFactory>)createCoreChannelFactoryWithCallOptions:(GRPCCallOptions *)callOptions {
  return [GRPCInsecureChannelFactory sharedInstance];
}

@end
