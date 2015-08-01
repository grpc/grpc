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

#import <grpc/grpc_security.h>

@implementation GRPCSecureChannel

- (instancetype)initWithHost:(NSString *)host {
  static grpc_credentials *kCredentials;
  static dispatch_once_t loading;
  dispatch_once(&loading, ^{
    // Do not use NSBundle.mainBundle, as it's nil for tests of library projects.
    NSBundle *bundle = [NSBundle bundleForClass:self.class];
    NSString *certsPath = [bundle pathForResource:@"gRPCCertificates.bundle/roots" ofType:@"pem"];
    NSAssert(certsPath.length,
             @"gRPCCertificates.bundle/roots.pem not found under %@. This file, with the root "
             "certificates, is needed to establish TLS (HTTPS) connections.", bundle.bundlePath);
    NSData *certsData = [NSData dataWithContentsOfFile:certsPath];
    NSAssert(certsData.length, @"No data read from %@", certsPath);
    NSString *certsString = [[NSString alloc] initWithData:certsData encoding:NSUTF8StringEncoding];
    kCredentials = grpc_ssl_credentials_create(certsString.UTF8String, NULL);
  });
  return (self = [super initWithChannel:grpc_secure_channel_create(kCredentials,
                                                                   host.UTF8String,
                                                                   NULL)
                               hostName:host]);
}

@end
