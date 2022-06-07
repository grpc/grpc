/**
 * Copyright 2022 gRPC authors.
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
 */

#import "TestUtils.h"

// Utility macro to stringize preprocessor defines
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)

NSString *GRPCGetLocalInteropTestServerAddressPlainText() {
  static NSString *address;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    address =
        [NSProcessInfo processInfo].environment[@"HOST_PORT_LOCAL"] ?: NSStringize(HOST_PORT_LOCAL);
  });
  return address;
}

NSString *GRPCGetLocalInteropTestServerAddressSSL() {
  static NSString *address;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    address = [NSProcessInfo processInfo].environment[@"HOST_PORT_LOCALSSL"]
                  ?: NSStringize(HOST_PORT_LOCALSSL);
  });
  return address;
}

NSString *GRPCGetRemoteInteropTestServerAddress() {
  static NSString *address;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    address = [NSProcessInfo processInfo].environment[@"HOST_PORT_REMOTE"]
                  ?: NSStringize(HOST_PORT_REMOTE);
  });
  return address;
}

void GRPCPrintInteropTestServerDebugInfo() {
  NSLog(@"local interop env: %@  macro: %@",
        [NSProcessInfo processInfo].environment[@"HOST_PORT_LOCAL"], NSStringize(HOST_PORT_LOCAL));
  NSLog(@"local interop ssl env: %@  macro: %@",
        [NSProcessInfo processInfo].environment[@"HOST_PORT_LOCALSSL"],
        NSStringize(HOST_PORT_LOCALSSL));
  NSLog(@"remote interop env: %@  macro: %@",
        [NSProcessInfo processInfo].environment[@"HOST_PORT_REMOTE"],
        NSStringize(HOST_PORT_REMOTE));
}
