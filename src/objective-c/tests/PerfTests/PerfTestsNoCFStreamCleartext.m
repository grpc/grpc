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

#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/GRPCTransport.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>

#import "PerfTests.h"

// The server address is derived from preprocessor macro, which is
// in turn derived from environment variable of the same name.
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *const kLocalCleartextHost = NSStringize(HOST_PORT_LOCAL);

extern const char *kCFStreamVarName;

// The Protocol Buffers encoding overhead of local interop server. Acquired
// by experiment. Adjust this when server's proto file changes.
static int32_t kLocalInteropServerOverhead = 10;

/** Tests in PerfTests.m, sending the RPCs to a local cleartext server. */
@interface PerfTestsNoCFStreamCleartext : PerfTests
@end

@implementation PerfTestsNoCFStreamCleartext

+ (NSString *)host {
  return kLocalCleartextHost;
}

+ (NSString *)PEMRootCertificates {
  return nil;
}

+ (NSString *)hostNameOverride {
  return nil;
}

- (int32_t)encodingOverhead {
  return kLocalInteropServerOverhead;  // bytes
}

+ (void)setUp {
  setenv(kCFStreamVarName, "0", 1);
}

- (void)setUp {
  [super setUp];

  // Register test server as non-SSL.
  [GRPCCall useInsecureConnectionsForHost:kLocalCleartextHost];
}

+ (GRPCTransportID)transport {
  return GRPCDefaultTransportImplList.core_insecure;
}

@end
