/*
 *
 * Copyright 2016 gRPC authors.
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

#import "GRPCCall+ChannelCredentials.h"

#import "private/GRPCHost.h"

@implementation GRPCCall (ChannelCredentials)

+ (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                   forHost:(nonnull NSString *)host
                     error:(NSError **)errorPtr {
  if (!host) {
    [NSException raise:NSInvalidArgumentException
                format:@"host must be provided."];
  }
  GRPCHost *hostConfig = [GRPCHost hostWithAddress:host];
  return [hostConfig setTLSPEMRootCerts:pemRootCerts
                 withPrivateKey:pemPrivateKey
                  withCertChain:pemCertChain
                          error:errorPtr];
}

+ (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
                   forHost:(nonnull NSString *)host
                     error:(NSError **)errorPtr {
  return [GRPCCall setTLSPEMRootCerts:pemRootCerts
               withPrivateKey:nil
                withCertChain:nil
                      forHost:host
                      error:errorPtr];
}

@end
