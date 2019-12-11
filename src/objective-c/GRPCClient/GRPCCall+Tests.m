/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#import "GRPCCall+Tests.h"

#import "private/GRPCCore/GRPCHost.h"

#import "GRPCCallOptions.h"

@implementation GRPCCall (Tests)

+ (void)useTestCertsPath:(NSString *)certsPath
                testName:(NSString *)testName
                 forHost:(NSString *)host {
  if (!host) {
    [NSException raise:NSInvalidArgumentException format:@"host must be provided."];
  }
  if (!certsPath) {
    [NSException raise:NSInvalidArgumentException format:@"certpath be provided."];
  }
  if (!testName) {
    [NSException raise:NSInvalidArgumentException format:@"testname must be provided."];
  }
  NSError *error = nil;
  NSString *certs =
      [NSString stringWithContentsOfFile:certsPath encoding:NSUTF8StringEncoding error:&error];
  if (error != nil) {
    [NSException raise:[error localizedDescription] format:@"failed to load certs"];
  }

  GRPCHost *hostConfig = [GRPCHost hostWithAddress:host];
  [hostConfig setTLSPEMRootCerts:certs withPrivateKey:nil withCertChain:nil error:nil];
  hostConfig.hostNameOverride = testName;
}

+ (void)useInsecureConnectionsForHost:(NSString *)host {
  GRPCHost *hostConfig = [GRPCHost hostWithAddress:host];
  hostConfig.transportType = GRPCTransportTypeInsecure;
}

+ (void)resetHostSettings {
  [GRPCHost resetAllHostSettings];
}
@end
