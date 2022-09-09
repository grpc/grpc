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

#import <XCTest/XCTest.h>

#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+Tests.h>

// Utility macro to stringize preprocessor defines
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)

// Default test flake repeat counts
static const NSUInteger kGRPCDefaultTestFlakeRepeats = 1;

// Default interop local test timeout.
const NSTimeInterval GRPCInteropTestTimeoutDefault = 15.0;

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

// Helper function to retrieve falke repeat from env variable settings.
static NSUInteger GRPCGetTestFlakeRepeats() {
  static NSUInteger repeats = kGRPCDefaultTestFlakeRepeats;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    NSString *repeatStr = [NSProcessInfo processInfo].environment[@"FLAKE_TEST_REPEATS"];
    if (repeatStr != nil) {
      repeats = [repeatStr integerValue];
    }
  });
  return repeats;
}

void GRPCResetCallConnections() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [GRPCCall closeOpenConnections];
#pragma clang diagnostic pop
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

BOOL GRPCTestRunWithFlakeRepeats(XCTestCase *testCase, GRPCTestRunBlock testBlock) {
  NSInteger repeats = GRPCGetTestFlakeRepeats();
  NSInteger runs = 0;

  while (runs < repeats) {
    GRPCResetCallConnections();

    const BOOL isLastRun = (runs == repeats - 1);
    __block XCTWaiterResult result;
    __block BOOL assertionSuccess = YES;

    GRPCTestWaiter waiterBlock =
        ^(NSArray<XCTestExpectation *> *expectations, NSTimeInterval timeout) {
          if (isLastRun) {
            XCTWaiter *waiter = [[XCTWaiter alloc] initWithDelegate:testCase];
            result = [waiter waitForExpectations:expectations timeout:timeout];
          } else {
            result = [XCTWaiter waitForExpectations:expectations timeout:timeout];
          }
        };

    GRPCTestAssert assertBlock = ^(BOOL expressionValue, NSString *message) {
      BOOL result = !!(expressionValue);
      assertionSuccess = assertionSuccess && result;
      if (isLastRun && !result) {
        _XCTPrimitiveFail(testCase, @"%@", message);
      }
    };

    testBlock(waiterBlock, assertBlock);

    if (result == XCTWaiterResultCompleted && assertionSuccess) {
      return YES;
    }

    if (!isLastRun) {
      NSLog(@"test attempt %@ failed, will retry.", NSStringize(runs));
    }
    runs += 1;
  }

  return NO;
}
