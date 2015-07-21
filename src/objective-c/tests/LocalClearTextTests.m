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

#import <GRPCClient/GRPCCall.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RouteGuide/RouteGuide.pbobjc.h>
#import <RouteGuide/RouteGuide.pbrpc.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Immediate.h>

// These tests require a gRPC "RouteGuide" sample server to be running locally. You can compile and
// run one by following the instructions here: https://github.com/grpc/grpc-common/blob/master/cpp/cpptutorial.md#try-it-out
// Be sure to have the C gRPC library installed in your system (for example, by having followed the
// instructions at https://github.com/grpc/homebrew-grpc

static NSString * const kRouteGuideHost = @"http://localhost:50051";
static NSString * const kPackage = @"examples";
static NSString * const kService = @"RouteGuide";

@interface LocalClearTextTests : XCTestCase
@end

@implementation LocalClearTextTests

// This test currently fails: see Issue #1907.
//- (void)testConnectionToLocalServer {
//  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Server reachable."];
//
//  // This method isn't implemented by the local server.
//  GRPCMethodName *method = [[GRPCMethodName alloc] initWithPackage:kPackage
//                                                         interface:kService
//                                                            method:@"EmptyCall"];
//
//  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[NSData data]];
//
//  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRouteGuideHost
//                                           method:method
//                                   requestsWriter:requestsWriter];
//
//  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
//    XCTFail(@"Received unexpected response: %@", value);
//  } completionHandler:^(NSError *errorOrNil) {
//    XCTAssertNotNil(errorOrNil, @"Finished without error!");
//    XCTAssertEqual(errorOrNil.code, 12, @"Finished with unexpected error: %@", errorOrNil);
//    [expectation fulfill];
//  }];
//
//  [call startWithWriteable:responsesWriteable];
//
//  [self waitForExpectationsWithTimeout:8.0 handler:nil];
//}

- (void)testEmptyRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  ProtoMethod *method = [[ProtoMethod alloc] initWithPackage:kPackage
                                                     service:kService
                                                      method:@"RecordRoute"];

  GRXWriter *requestsWriter = [GRXWriter emptyWriter];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRouteGuideHost
                                             path:method.HTTPPath
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

  ProtoMethod *method = [[ProtoMethod alloc] initWithPackage:kPackage
                                                     service:kService
                                                      method:@"GetFeature"];

  RGDPoint *point = [RGDPoint message];
  point.latitude = 28E7;
  point.longitude = -15E7;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[point data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRouteGuideHost
                                             path:method.HTTPPath
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

  RGDRouteGuide *service = [[RGDRouteGuide alloc] initWithHost:kRouteGuideHost];
  [service getFeatureWithRequest:point handler:^(RGDFeature *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);
    XCTAssertEqualObjects(point, response.location);
    XCTAssertNotNil(response.name, @"Response's name is nil.");
    [completion fulfill];
  }];

  [self waitForExpectationsWithTimeout:2.0 handler:nil];
}
@end
