/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import <GRPCClient/GRPCCall+Tests.h>

#import "InteropTests.h"

static NSString * const kRemoteSSLHost = @"grpc-test.sandbox.googleapis.com";

/** Tests in InteropTests.m, sending the RPCs to a remote SSL server. */
@interface InteropTestsRemoteWithCronet : InteropTests
@end

@implementation InteropTestsRemoteWithCronet

+ (NSString *)host {
  return kRemoteSSLHost;
}

@end
