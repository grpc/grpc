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

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>

#import <RemoteTest/Messages.pb.h>
#import <RemoteTest/Test.pb.h>

@interface RemoteProtoTests : XCTestCase
@end

@implementation RemoteProtoTests {
  RMTTestService *_service;
}

- (void)setUp {
  _service = [[RMTTestService alloc] initWithHost:@"grpc-test.sandbox.google.com"];
}

- (void)testEmptyRPC {
  __weak XCTestExpectation *noRPCError = [self expectationWithDescription:@"RPC succeeded."];
  __weak XCTestExpectation *responded = [self expectationWithDescription:@"Response received."];

  [_service emptyCallWithRequest:[RMTEmpty defaultInstance]
                         handler:^(RMTEmpty *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);
    [noRPCError fulfill];
    XCTAssertNotNil(response, @"nil response received.");
    [responded fulfill];
  }];

  [self waitForExpectationsWithTimeout:2. handler:nil];
}

- (void)testSimpleProtoRPC {
  __weak XCTestExpectation *noRPCError = [self expectationWithDescription:@"RPC succeeded."];
  __weak XCTestExpectation *responded = [self expectationWithDescription:@"Response received."];
  __weak XCTestExpectation *validResponse = [self expectationWithDescription:@"Valid response."];

  RMTSimpleRequest *request = [[[[[[RMTSimpleRequestBuilder alloc] init]
                                  setResponseSize:100]
                                 setFillUsername:YES]
                                setFillOauthScope:YES]
                               build];
  [_service unaryCallWithRequest:request handler:^(RMTSimpleResponse *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);
    [noRPCError fulfill];
    XCTAssertNotNil(response, @"nil response received.");
    [responded fulfill];
    // We expect empty strings, not nil:
    XCTAssertNotNil(response.username, @"Response's username is nil.");
    XCTAssertNotNil(response.oauthScope, @"Response's OAuth scope is nil.");
    [validResponse fulfill];
  }];

  [self waitForExpectationsWithTimeout:2. handler:nil];
}

@end
