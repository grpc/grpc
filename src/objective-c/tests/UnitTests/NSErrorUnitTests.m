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

#import <XCTest/XCTest.h>

#import <GRPCClient/GRPCCall.h>

#import "../../GRPCClient/private/GRPCCore/NSError+GRPC.h"

@interface NSErrorUnitTests : XCTestCase

@end

@implementation NSErrorUnitTests

- (void)testNSError {
  const char *kDetails = "test details";
  const char *kErrorString = "test errorString";
  NSError *error1 = [NSError grpc_errorFromStatusCode:GRPC_STATUS_OK details:nil errorString:nil];
  NSError *error2 = [NSError grpc_errorFromStatusCode:GRPC_STATUS_CANCELLED
                                              details:kDetails
                                          errorString:kErrorString];
  NSError *error3 = [NSError grpc_errorFromStatusCode:GRPC_STATUS_UNAUTHENTICATED
                                              details:kDetails
                                          errorString:nil];
  NSError *error4 = [NSError grpc_errorFromStatusCode:GRPC_STATUS_UNAVAILABLE
                                              details:nil
                                          errorString:nil];

  XCTAssertNil(error1);
  XCTAssertEqual(error2.code, 1);
  XCTAssertEqualObjects(error2.domain, @"io.grpc");
  XCTAssertEqualObjects(error2.userInfo[NSLocalizedDescriptionKey],
                        [NSString stringWithUTF8String:kDetails]);
  XCTAssertEqualObjects(error2.userInfo[NSDebugDescriptionErrorKey],
                        [NSString stringWithUTF8String:kErrorString]);
  XCTAssertEqual(error3.code, 16);
  XCTAssertEqualObjects(error3.domain, @"io.grpc");
  XCTAssertEqualObjects(error3.userInfo[NSLocalizedDescriptionKey],
                        [NSString stringWithUTF8String:kDetails]);
  XCTAssertNil(error3.userInfo[NSDebugDescriptionErrorKey]);
  XCTAssertEqual(error4.code, 14);
  XCTAssertEqualObjects(error4.domain, @"io.grpc");
  XCTAssertNil(error4.userInfo[NSLocalizedDescriptionKey]);
  XCTAssertNil(error4.userInfo[NSDebugDescriptionErrorKey]);
}

@end
