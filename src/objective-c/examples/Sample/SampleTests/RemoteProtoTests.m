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

#include <grpc/status.h>

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>

#import <gRPC/ProtoRPC.h>
#import <gRPC/GRXWriter+Immediate.h>
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

// Tests as described here: https://github.com/grpc/grpc/blob/master/doc/interop-test-descriptions.md

- (void)testEmptyUnaryRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyUnary"];

  RMTEmpty *request = [RMTEmpty defaultInstance];

  [_service emptyCallWithRequest:request handler:^(RMTEmpty *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);

    id expectedResponse = [RMTEmpty defaultInstance];
    XCTAssertEqualObjects(response, expectedResponse);

    [expectation fulfill];
  }];

  [self waitForExpectationsWithTimeout:2. handler:nil];
}

- (void)testLargeUnaryRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

  RMTSimpleRequest *request = [[[[[[RMTSimpleRequestBuilder alloc] init]
                                  setResponseType:RMTPayloadTypeCompressable]
                                 setResponseSize:314159]
                                setPayloadBuilder:[[[RMTPayloadBuilder alloc] init]
                                             setBody:[NSMutableData dataWithLength:271828]]]
                               build];

  [_service unaryCallWithRequest:request handler:^(RMTSimpleResponse *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);

    id expectedResponse = [[[[RMTSimpleResponseBuilder alloc] init]
                            setPayloadBuilder:[[[[RMTPayloadBuilder alloc] init]
                                                setType:RMTPayloadTypeCompressable]
                                               setBody:[NSMutableData dataWithLength:314159]]]
                           build];
    XCTAssertEqualObjects(response, expectedResponse);

    [expectation fulfill];
  }];

  [self waitForExpectationsWithTimeout:4. handler:nil];
}

- (void)testClientStreamingRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ClientStreaming"];

  id request1 = [[[[RMTStreamingInputCallRequestBuilder alloc] init]
                  setPayloadBuilder:[[[RMTPayloadBuilder alloc] init]
                                     setBody:[NSMutableData dataWithLength:27182]]]
                 build];
  id request2 = [[[[RMTStreamingInputCallRequestBuilder alloc] init]
                  setPayloadBuilder:[[[RMTPayloadBuilder alloc] init]
                                     setBody:[NSMutableData dataWithLength:8]]]
                 build];
  id request3 = [[[[RMTStreamingInputCallRequestBuilder alloc] init]
                  setPayloadBuilder:[[[RMTPayloadBuilder alloc] init]
                                     setBody:[NSMutableData dataWithLength:1828]]]
                 build];
  id request4 = [[[[RMTStreamingInputCallRequestBuilder alloc] init]
                  setPayloadBuilder:[[[RMTPayloadBuilder alloc] init]
                                     setBody:[NSMutableData dataWithLength:45904]]]
                 build];
  id<GRXWriter> writer = [GRXWriter writerWithContainer:@[request1, request2, request3, request4]];

  [_service streamingInputCallWithRequestsWriter:writer
                                         handler:^(RMTStreamingInputCallResponse *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);

    id expectedResponse = [[[[RMTStreamingInputCallResponseBuilder alloc] init]
                            setAggregatedPayloadSize:74922]
                           build];
    XCTAssertEqualObjects(response, expectedResponse);

    [expectation fulfill];
  }];

  [self waitForExpectationsWithTimeout:4. handler:nil];
}

- (void)testServerStreamingRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ServerStreaming"];
  NSArray *expectedSizes = @[@31415, @9, @2653, @58979];
  __block int index = 0;
  id request = [[[[[[[RMTStreamingOutputCallRequestBuilder alloc] init]
                    addResponseParameters:[[[[RMTResponseParametersBuilder alloc] init]
                                            setSize:31415] build]]
                   addResponseParameters:[[[[RMTResponseParametersBuilder alloc] init]
                                           setSize:9] build]]
                  addResponseParameters:[[[[RMTResponseParametersBuilder alloc] init]
                                          setSize:2653] build]]
                 addResponseParameters:[[[[RMTResponseParametersBuilder alloc] init]
                                         setSize:58979] build]]
                build];
  [_service streamingOutputCallWithRequest:request handler:^(BOOL done, RMTStreamingOutputCallResponse *response, NSError *error){
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);
    id expectedResponseBuilder = [[RMTStreamingOutputCallResponseBuilder alloc] init];
    id expectedPayload = [[[[[RMTPayloadBuilder alloc] init]
                            setType:RMTPayloadTypeCompressable]
                           setBody:[NSMutableData dataWithLength:[expectedSizes[index] unsignedIntegerValue]]]
                          build];
    expectedResponseBuilder = [expectedResponseBuilder setPayload:expectedPayload];
    id expectedResponse = [expectedResponseBuilder build];
    XCTAssertEqualObjects(response, expectedResponse);
    
    [expectation fulfill];
    index += 1;
  }];
  
  [self waitForExpectationsWithTimeout:4 handler:nil];
}

- (void)testEmptyStreamRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyStream"];
  [_service fullDuplexCallWithRequestsWriter:[GRXWriter emptyWriter]
                                     handler:^(bool done, RMTStreamingOutputCallResponse *response, NSError *error) {
                                       XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                                       XCTAssert(done, @"Unexpected response: %@", response);
                                       [expectation fulfill];
                                     }];
  [self waitForExpectationsWithTimeout:4 handler:nil];
}

- (void)testCancelAfterBeginRPC {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"CancelAfterBegin"];
  // TODO(mlumish): change to writing that blocks instead of writing
  ProtoRPC *call = [_service RPCToStreamingInputCallWithRequestsWriter:[GRXWriter emptyWriter]
                                                               handler:^(RMTStreamingInputCallResponse *response, NSError *error) {
                                                                 XCTAssertEqual([error code], GRPC_STATUS_CANCELLED);
                                                                 [expectation fulfill];
                                                               }];
  [call start];
  [call cancel];
  [self waitForExpectationsWithTimeout:1 handler:nil];
}

@end
