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

#import "GRPCHost.h"

#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCCallOptions.h>
#import <GRPCClient/GRPCTransport.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#import "../../internal/GRPCCallOptions+Internal.h"
#import "GRPCChannelFactory.h"
#import "GRPCCompletionQueue.h"
#import "GRPCSecureChannelFactory.h"
#import "NSDictionary+GRPC.h"

NS_ASSUME_NONNULL_BEGIN

static NSMutableDictionary *gHostCache;

@implementation GRPCHost {
  NSString *_PEMRootCertificates;
  NSString *_PEMPrivateKey;
  NSString *_PEMCertificateChain;
}

+ (nullable instancetype)hostWithAddress:(NSString *)address {
  return [[self alloc] initWithAddress:address];
}

// Default initializer.
- (nullable instancetype)initWithAddress:(NSString *)address {
  if (!address) {
    return nil;
  }

  // To provide a default port, we try to interpret the address. If it's just a host name without
  // scheme and without port, we'll use port 443. If it has a scheme, we pass it untouched to the C
  // gRPC library.
  // TODO(jcanizales): Add unit tests for the types of addresses we want to let pass untouched.
  NSURL *hostURL = [NSURL URLWithString:[@"https://" stringByAppendingString:address]];
  if (hostURL.host && hostURL.port == nil) {
    address = [hostURL.host stringByAppendingString:@":443"];
  }

  // Look up the GRPCHost in the cache.
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    gHostCache = [NSMutableDictionary dictionary];
  });
  @synchronized(gHostCache) {
    GRPCHost *cachedHost = gHostCache[address];
    if (cachedHost) {
      return cachedHost;
    }

    if ((self = [super init])) {
      _address = [address copy];
      _retryEnabled = YES;
      gHostCache[address] = self;
    }
  }
  return self;
}

+ (void)resetAllHostSettings {
  @synchronized(gHostCache) {
    gHostCache = [NSMutableDictionary dictionary];
  }
}

- (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                     error:(NSError **)errorPtr {
  _PEMRootCertificates = [pemRootCerts copy];
  _PEMPrivateKey = [pemPrivateKey copy];
  _PEMCertificateChain = [pemCertChain copy];
  return YES;
}

- (GRPCCallOptions *)callOptions {
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.userAgentPrefix = _userAgentPrefix;
  options.responseSizeLimit = _responseSizeLimitOverride;
  options.compressionAlgorithm = (GRPCCompressionAlgorithm)_compressAlgorithm;
  options.retryEnabled = _retryEnabled;
  options.keepaliveInterval = (NSTimeInterval)_keepaliveInterval / 1000;
  options.keepaliveTimeout = (NSTimeInterval)_keepaliveTimeout / 1000;
  options.connectMinTimeout = (NSTimeInterval)_minConnectTimeout / 1000;
  options.connectInitialBackoff = (NSTimeInterval)_initialConnectBackoff / 1000;
  options.connectMaxBackoff = (NSTimeInterval)_maxConnectBackoff / 1000;
  options.PEMRootCertificates = _PEMRootCertificates;
  options.PEMPrivateKey = _PEMPrivateKey;
  options.PEMCertificateChain = _PEMCertificateChain;
  options.hostNameOverride = _hostNameOverride;
  if (_transportType == GRPCTransportTypeInsecure) {
    options.transport = GRPCDefaultTransportImplList.core_insecure;
  } else if ([GRPCCall isUsingCronet]) {
    options.transport = gGRPCCoreCronetID;
  } else {
    options.transport = GRPCDefaultTransportImplList.core_secure;
  }
  options.logContext = _logContext;

  return options;
}

+ (GRPCCallOptions *)callOptionsForHost:(NSString *)host {
  // TODO (mxyan): Remove when old API is deprecated
  GRPCCallOptions *callOptions = nil;
  @synchronized(gHostCache) {
    GRPCHost *hostConfig = [GRPCHost hostWithAddress:host];
    callOptions = [hostConfig callOptions];
  }
  NSAssert(callOptions != nil, @"Unable to create call options object");
  if (callOptions == nil) {
    NSLog(@"Unable to create call options object");
    callOptions = [[GRPCCallOptions alloc] init];
  }
  return callOptions;
}

@end

NS_ASSUME_NONNULL_END
