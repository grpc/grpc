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

#import "GRPCSecureChannelFactory.h"

#include <grpc/grpc_security.h>

#import "ChannelArgsUtil.h"
#import "GRPCChannel.h"

@implementation GRPCSecureChannelFactory {
  grpc_channel_credentials *_channelCreds;
}

+ (instancetype)factoryWithPEMRootCertificates:(NSString *)rootCerts
                                    privateKey:(NSString *)privateKey
                                     certChain:(NSString *)certChain
                                         error:(NSError **)errorPtr {
  return [[self alloc] initWithPEMRootCerts:rootCerts
                                 privateKey:privateKey
                                  certChain:certChain
                                      error:errorPtr];
}

- (NSData *)nullTerminatedDataWithString:(NSString *)string {
  // dataUsingEncoding: does not return a null-terminated string.
  NSData *data = [string dataUsingEncoding:NSASCIIStringEncoding allowLossyConversion:YES];
  if (data == nil) {
    return nil;
  }
  NSMutableData *nullTerminated = [NSMutableData dataWithData:data];
  [nullTerminated appendBytes:"\0" length:1];
  return nullTerminated;
}

- (instancetype)initWithPEMRootCerts:(NSString *)rootCerts
                          privateKey:(NSString *)privateKey
                           certChain:(NSString *)certChain
                               error:(NSError **)errorPtr {
  static NSData *defaultRootsASCII;
  static NSError *defaultRootsError;
  static dispatch_once_t loading;
  dispatch_once(&loading, ^{
    NSString *defaultPath = @"gRPCCertificates.bundle/roots";  // .pem
    // Do not use NSBundle.mainBundle, as it's nil for tests of library projects.
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *path = [bundle pathForResource:defaultPath ofType:@"pem"];
    NSError *error;
    // Files in PEM format can have non-ASCII characters in their comments (e.g. for the name of the
    // issuer). Load them as UTF8 and produce an ASCII equivalent.
    NSString *contentInUTF8 =
        [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
    if (contentInUTF8 == nil) {
      defaultRootsError = error;
      return;
    }
    defaultRootsASCII = [self nullTerminatedDataWithString:contentInUTF8];
  });

  NSData *rootsASCII;
  if (rootCerts != nil) {
    rootsASCII = [self nullTerminatedDataWithString:rootCerts];
  } else {
    if (defaultRootsASCII == nil) {
      if (errorPtr) {
        *errorPtr = defaultRootsError;
      }
      NSAssert(
          defaultRootsASCII, NSObjectNotAvailableException,
          @"Could not read gRPCCertificates.bundle/roots.pem. This file, "
           "with the root certificates, is needed to establish secure (TLS) connections. "
           "Because the file is distributed with the gRPC library, this error is usually a sign "
           "that the library wasn't configured correctly for your project. Error: %@",
          defaultRootsError);
      return nil;
    }
    rootsASCII = defaultRootsASCII;
  }

  grpc_channel_credentials *creds = NULL;
  if (privateKey.length == 0 && certChain.length == 0) {
    creds = grpc_ssl_credentials_create(rootsASCII.bytes, NULL, NULL, NULL);
  } else {
    grpc_ssl_pem_key_cert_pair key_cert_pair;
    NSData *privateKeyASCII = [self nullTerminatedDataWithString:privateKey];
    NSData *certChainASCII = [self nullTerminatedDataWithString:certChain];
    key_cert_pair.private_key = privateKeyASCII.bytes;
    key_cert_pair.cert_chain = certChainASCII.bytes;
    if (key_cert_pair.private_key == NULL || key_cert_pair.cert_chain == NULL) {
      creds = grpc_ssl_credentials_create(rootsASCII.bytes, NULL, NULL, NULL);
    } else {
      creds = grpc_ssl_credentials_create(rootsASCII.bytes, &key_cert_pair, NULL, NULL);
    }
  }

  if ((self = [super init])) {
    _channelCreds = creds;
  }
  return self;
}

- (grpc_channel *)createChannelWithHost:(NSString *)host channelArgs:(NSDictionary *)args {
  NSAssert(host.length != 0, @"host cannot be empty");
  if (host.length == 0) {
    return NULL;
  }
  grpc_channel_args *coreChannelArgs = GRPCBuildChannelArgs(args);
  grpc_channel *unmanagedChannel =
      grpc_secure_channel_create(_channelCreds, host.UTF8String, coreChannelArgs, NULL);
  GRPCFreeChannelArgs(coreChannelArgs);
  return unmanagedChannel;
}

- (void)dealloc {
  if (_channelCreds != NULL) {
    grpc_channel_credentials_release(_channelCreds);
  }
}

@end
