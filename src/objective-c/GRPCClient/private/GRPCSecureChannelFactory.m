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
  static dispatch_once_t loading;
  dispatch_once(&loading, ^{
    NSString *defaultPath = @"gRPCCertificates.bundle/roots";  // .pem
    // Do not use NSBundle.mainBundle, as it's nil for tests of library projects.
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *path = [bundle pathForResource:defaultPath ofType:@"pem"];
    setenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR,
           [path cStringUsingEncoding:NSUTF8StringEncoding], 1);
  });

  NSData *rootsASCII = nil;
  // if rootCerts is not provided, gRPC will use its own default certs
  if (rootCerts != nil) {
    rootsASCII = [self nullTerminatedDataWithString:rootCerts];
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
