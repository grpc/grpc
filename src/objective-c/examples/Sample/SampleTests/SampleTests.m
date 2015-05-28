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

#import <gRPC/GRPCCall.h>
#import <gRPC/GRPCMethodName.h>
#import <gRPC/GRXWriter+Immediate.h>
#import <gRPC/GRXWriteable.h>
#import <Route_guide/RouteGuide.pbobjc.h>
#import <Route_guide/RouteGuide.pbrpc.h>

@interface SampleTests : XCTestCase
@end

// These tests require the gRPC-Java "RouteGuide" sample server to be running locally. Install the
// gRPC-Java library following the instructions here: https://github.com/grpc/grpc-java And run the
// server by following the instructions here: https://github.com/grpc/grpc-java/tree/master/examples
@implementation SampleTests

- (void)testConnectionToLocalServer {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Server reachable."];

  // This method isn't implemented by the local server.
  GRPCMethodName *method = [[GRPCMethodName alloc] initWithPackage:@"grpc.testing"
                                                         interface:@"TestService"
                                                            method:@"EmptyCall"];

  id<GRXWriter> requestsWriter = [GRXWriter writerWithValue:[NSData data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:@"http://127.0.0.1:8980"
                                           method:method
                                   requestsWriter:requestsWriter];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTFail(@"Received unexpected response: %@", value);
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNotNil(errorOrNil, @"Finished without error!");
    XCTAssertEqual(errorOrNil.code, 12, @"Finished with unexpected error: %@", errorOrNil);
    [expectation fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:2.0 handler:nil];
}

- (void)testEmptyRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCMethodName *method = [[GRPCMethodName alloc] initWithPackage:@"grpc.example.routeguide"
                                                         interface:@"RouteGuide"
                                                            method:@"RecordRoute"];

  id<GRXWriter> requestsWriter = [GRXWriter emptyWriter];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:@"http://127.0.0.1:8980"
                                           method:method
                                   requestsWriter:requestsWriter];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
    [completion fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:2.0 handler:nil];
}

- (void)testSimpleProtoRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRPCMethodName *method = [[GRPCMethodName alloc] initWithPackage:@"grpc.example.routeguide"
                                                         interface:@"RouteGuide"
                                                            method:@"GetFeature"];

  RGDPoint *point = [RGDPoint message];
  point.latitude = 28E7;
  point.longitude = -15E7;
  id<GRXWriter> requestsWriter = [GRXWriter writerWithValue:[point data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:@"http://127.0.0.1:8980"
                                           method:method
                                   requestsWriter:requestsWriter];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    RGDFeature *feature = [RGDFeature parseFromData:value error:NULL];
    XCTAssertEqualObjects(point, feature.location);
    XCTAssertNotNil(feature.name, @"Response's name is nil.");
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
    [completion fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:2.0 handler:nil];
}

- (void)testSimpleProtoRPCUsingGeneratedService {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RGDPoint *point = [RGDPoint message];
  point.latitude = 28E7;
  point.longitude = -15E7;

  RGDRouteGuide *service = [[RGDRouteGuide alloc] initWithHost:@"http://127.0.0.1:8980"];
  [service getFeatureWithRequest:point handler:^(RGDFeature *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);
    XCTAssertEqualObjects(point, response.location);
    XCTAssertNotNil(response.name, @"Response's name is nil.");
    [completion fulfill];
  }];

  [self waitForExpectationsWithTimeout:2.0 handler:nil];
}
@end
