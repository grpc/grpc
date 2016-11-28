/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import "GRPCHost.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#import <GRPCClient/GRPCCall.h>
#ifdef GRPC_COMPILE_WITH_CRONET
#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+Cronet.h>
#endif

#import "GRPCChannel.h"
#import "GRPCCompletionQueue.h"
#import "NSDictionary+GRPC.h"

NS_ASSUME_NONNULL_BEGIN

// TODO(jcanizales): Generate the version in a standalone header, from templates. Like
// templates/src/core/surface/version.c.template .
#define GRPC_OBJC_VERSION_STRING @"1.0.2"

static NSMutableDictionary *kHostCache;

@implementation GRPCHost {
  // TODO(mlumish): Investigate whether caching channels with strong links is a good idea.
  GRPCChannel *_channel;
}

+ (nullable instancetype)hostWithAddress:(NSString *)address {
  return [[self alloc] initWithAddress:address];
}

- (void)dealloc {
  if (_channelCreds != nil) {
    grpc_channel_credentials_release(_channelCreds);
  }
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
  if (hostURL.host && !hostURL.port) {
    address = [hostURL.host stringByAppendingString:@":443"];
  }

  // Look up the GRPCHost in the cache.
  static dispatch_once_t cacheInitialization;
  dispatch_once(&cacheInitialization, ^{
    kHostCache = [NSMutableDictionary dictionary];
  });
  @synchronized(kHostCache) {
    GRPCHost *cachedHost = kHostCache[address];
    if (cachedHost) {
      return cachedHost;
    }

    if ((self = [super init])) {
      _address = address;
      _secure = YES;
      kHostCache[address] = self;
    }
  }
  return self;
}

+ (void)flushChannelCache {
  @synchronized(kHostCache) {
    [kHostCache enumerateKeysAndObjectsUsingBlock:^(id  _Nonnull key,
                                                    GRPCHost * _Nonnull host,
                                                    BOOL * _Nonnull stop) {
      [host disconnect];
    }];
  }
}

+ (void)resetAllHostSettings {
  @synchronized (kHostCache) {
    kHostCache = [NSMutableDictionary dictionary];
  }
}

- (nullable grpc_call *)unmanagedCallWithPath:(NSString *)path
                              completionQueue:(GRPCCompletionQueue *)queue {
  GRPCChannel *channel;
  // This is racing -[GRPCHost disconnect].
  @synchronized(self) {
    if (!_channel) {
      _channel = [self newChannel];
    }
    channel = _channel;
  }
  return [channel unmanagedCallWithPath:path completionQueue:queue];
}

- (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                     error:(NSError **)errorPtr {
  static NSData *kDefaultRootsASCII;
  static NSError *kDefaultRootsError;
  static dispatch_once_t loading;
  dispatch_once(&loading, ^{
    NSString *defaultPath = @"gRPCCertificates.bundle/roots"; // .pem
    // Do not use NSBundle.mainBundle, as it's nil for tests of library projects.
    NSBundle *bundle = [NSBundle bundleForClass:self.class];
    NSString *path = [bundle pathForResource:defaultPath ofType:@"pem"];
    NSError *error;
    // Files in PEM format can have non-ASCII characters in their comments (e.g. for the name of the
    // issuer). Load them as UTF8 and produce an ASCII equivalent.
    NSString *contentInUTF8 = [NSString stringWithContentsOfFile:path
                                                        encoding:NSUTF8StringEncoding
                                                           error:&error];
    if (contentInUTF8 == nil) {
      kDefaultRootsError = error;
      return;
    }
    kDefaultRootsASCII = [contentInUTF8 dataUsingEncoding:NSASCIIStringEncoding
                                     allowLossyConversion:YES];
  });

  NSData *rootsASCII;
  if (pemRootCerts != nil) {
    rootsASCII = [pemRootCerts dataUsingEncoding:NSASCIIStringEncoding
                     allowLossyConversion:YES];
  } else {
    if (kDefaultRootsASCII == nil) {
      if (errorPtr) {
        *errorPtr = kDefaultRootsError;
      }
      NSAssert(kDefaultRootsASCII, @"Could not read gRPCCertificates.bundle/roots.pem. This file, "
               "with the root certificates, is needed to establish secure (TLS) connections. "
               "Because the file is distributed with the gRPC library, this error is usually a sign "
               "that the library wasn't configured correctly for your project. Error: %@",
                kDefaultRootsError);
      return NO;
    }
    rootsASCII = kDefaultRootsASCII;
  }

  grpc_channel_credentials *creds;
  if (pemPrivateKey == nil && pemCertChain == nil) {
    creds = grpc_ssl_credentials_create(rootsASCII.bytes, NULL, NULL);
  } else {
    grpc_ssl_pem_key_cert_pair key_cert_pair;
    NSData *privateKeyASCII = [pemPrivateKey dataUsingEncoding:NSASCIIStringEncoding
                                       allowLossyConversion:YES];
    NSData *certChainASCII = [pemCertChain dataUsingEncoding:NSASCIIStringEncoding
                                     allowLossyConversion:YES];
    key_cert_pair.private_key = privateKeyASCII.bytes;
    key_cert_pair.cert_chain = certChainASCII.bytes;
    creds = grpc_ssl_credentials_create(rootsASCII.bytes, &key_cert_pair, NULL);
  }

  @synchronized(self) {
    if (_channelCreds != nil) {
      grpc_channel_credentials_release(_channelCreds);
    }
    _channelCreds = creds;
  }

  return YES;
}

- (NSDictionary *)channelArgs {
  NSMutableDictionary *args = [NSMutableDictionary dictionary];

  // TODO(jcanizales): Add OS and device information (see
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#user-agents ).
  NSString *userAgent = @"grpc-objc/" GRPC_OBJC_VERSION_STRING;
  if (_userAgentPrefix) {
    userAgent = [_userAgentPrefix stringByAppendingFormat:@" %@", userAgent];
  }
  args[@GRPC_ARG_PRIMARY_USER_AGENT_STRING] = userAgent;

  if (_secure && _hostNameOverride) {
    args[@GRPC_SSL_TARGET_NAME_OVERRIDE_ARG] = _hostNameOverride;
  }

  if (_responseSizeLimitOverride) {
    args[@GRPC_ARG_MAX_MESSAGE_LENGTH] = _responseSizeLimitOverride;
  }
  // Use 10000ms initial backoff time for correct behavior on bad/slow networks  
  args[@GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS] = @10000;
  return args;
}

- (GRPCChannel *)newChannel {
  NSDictionary *args = [self channelArgs];
#ifdef GRPC_COMPILE_WITH_CRONET
  BOOL useCronet = [GRPCCall isUsingCronet];
#endif
  if (_secure) {
      GRPCChannel *channel;
      @synchronized(self) {
        if (_channelCreds == nil) {
          [self setTLSPEMRootCerts:nil withPrivateKey:nil withCertChain:nil error:nil];
        }
#ifdef GRPC_COMPILE_WITH_CRONET
        if (useCronet) {
          channel = [GRPCChannel secureCronetChannelWithHost:_address
                                                 channelArgs:args];
        } else
#endif
        {
          channel = [GRPCChannel secureChannelWithHost:_address
                                            credentials:_channelCreds
                                            channelArgs:args];
        }
      }
      return channel;
  } else {
    return [GRPCChannel insecureChannelWithHost:_address channelArgs:args];
  }
}

- (NSString *)hostName {
  // TODO(jcanizales): Default to nil instead of _address when Issue #2635 is clarified.
  return _hostNameOverride ?: _address;
}

- (void)disconnect {
  // This is racing -[GRPCHost unmanagedCallWithPath:completionQueue:].
  @synchronized(self) {
    _channel = nil;
  }
}

@end

NS_ASSUME_NONNULL_END
