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

#import "GRPCSecureChannel.h"

#include <grpc/grpc_security.h>

// Returns NULL if the file at path couldn't be read. In that case, if errorPtr isn't NULL,
// *errorPtr will be an object describing what went wrong.
static grpc_credentials *CertificatesAtPath(NSString *path, NSError **errorPtr) {
  NSString *certsContent = [NSString stringWithContentsOfFile:path
                                                     encoding:NSASCIIStringEncoding
                                                        error:errorPtr];
  if (!certsContent) {
    // Passing NULL to grpc_ssl_credentials_create produces behavior we don't want, so return.
    return NULL;
  }
  const char * asCString = [certsContent cStringUsingEncoding:NSASCIIStringEncoding];
  return grpc_ssl_credentials_create(asCString, NULL);
}

@implementation GRPCSecureChannel

- (instancetype)initWithHost:(NSString *)host {
  return [self initWithHost:host pathToCertificates:nil hostNameOverride:nil];
}

- (instancetype)initWithHost:(NSString *)host
          pathToCertificates:(NSString *)path
            hostNameOverride:(NSString *)hostNameOverride {
  // Load default SSL certificates once.
  static grpc_credentials *kDefaultCertificates;
  static dispatch_once_t loading;
  dispatch_once(&loading, ^{
    NSString *defaultPath = @"gRPCCertificates.bundle/roots"; // .pem
    // Do not use NSBundle.mainBundle, as it's nil for tests of library projects.
    NSBundle *bundle = [NSBundle bundleForClass:self.class];
    NSString *path = [bundle pathForResource:defaultPath ofType:@"pem"];
    NSError *error;
    kDefaultCertificates = CertificatesAtPath(path, &error);
    NSAssert(kDefaultCertificates, @"Could not read %@/%@.pem. This file, with the root "
             "certificates, is needed to establish secure (TLS) connections. Because the file is "
             "distributed with the gRPC library, this error is usually a sign that the library "
             "wasn't configured correctly for your project. Error: %@",
             bundle.bundlePath, defaultPath, error);
  });

  //TODO(jcanizales): Add NSError** parameter to the initializer.
  grpc_credentials *certificates = path ? CertificatesAtPath(path, NULL) : kDefaultCertificates;
  if (!certificates) {
    return nil;
  }

  // Ritual to pass the SSL host name override to the C library.
  grpc_channel_args channelArgs;
  grpc_arg nameOverrideArg;
  channelArgs.num_args = 1;
  channelArgs.args = &nameOverrideArg;
  nameOverrideArg.type = GRPC_ARG_STRING;
  nameOverrideArg.key = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG;
  // Cast const away. Hope C gRPC doesn't modify it!
  nameOverrideArg.value.string = (char *) hostNameOverride.UTF8String;
  grpc_channel_args *args = hostNameOverride ? &channelArgs : NULL;

  return [self initWithHost:host credentials:certificates args:args];
}

- (instancetype)initWithHost:(NSString *)host
                 credentials:(grpc_credentials *)credentials
                        args:(grpc_channel_args *)args {
  return (self =
          [super initWithChannel:grpc_secure_channel_create(credentials, host.UTF8String, args)]);
}

// TODO(jcanizales): GRPCSecureChannel and GRPCUnsecuredChannel are just convenience initializers
// for GRPCChannel. Move them into GRPCChannel, which will make the following unnecessary.
- (instancetype)initWithChannel:(grpc_channel *)unmanagedChannel {
  [NSException raise:NSInternalInconsistencyException format:@"use another initializer"];
  return [self initWithHost:nil]; // silence warnings
}

@end
