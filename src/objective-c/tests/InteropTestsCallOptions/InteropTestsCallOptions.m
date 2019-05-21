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

#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <grpc/grpc.h>

#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *kRemoteHost = NSStringize(HOST_PORT_REMOTE);
const int32_t kRemoteInteropServerOverhead = 12;

static const NSTimeInterval TEST_TIMEOUT = 16000;

@interface InteropTestsCallOptions : XCTestCase

@end

@implementation InteropTestsCallOptions {
  RMTTestService *_service;
}

- (void)setUp {
  self.continueAfterFailure = NO;
  _service = [RMTTestService serviceWithHost:kRemoteHost];
  _service.options = [[GRPCCallOptions alloc] init];
}

- (void)test4MBResponsesAreAccepted {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"MaxResponseSize"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const int32_t kPayloadSize =
      4 * 1024 * 1024 - kRemoteInteropServerOverhead;  // 4MB - encoding overhead
  request.responseSize = kPayloadSize;

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                           XCTAssertEqual(response.payload.body.length, kPayloadSize);
                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testResponsesOverMaxSizeFailWithActionableMessage {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ResponseOverMaxSize"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const int32_t kPayloadSize =
      4 * 1024 * 1024 - kRemoteInteropServerOverhead + 1;  // 1B over max size
  request.responseSize = kPayloadSize;

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertEqualObjects(
                               error.localizedDescription,
                               @"Received message larger than max (4194305 vs. 4194304)");
                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testResponsesOver4MBAreAcceptedIfOptedIn {
  __weak XCTestExpectation *expectation =
      [self expectationWithDescription:@"HigherResponseSizeLimit"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const size_t kPayloadSize = 5 * 1024 * 1024;  // 5MB
  request.responseSize = kPayloadSize;

  GRPCProtoCall *rpc = [_service
      RPCToUnaryCallWithRequest:request
                        handler:^(RMTSimpleResponse *response, NSError *error) {
                          XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                          XCTAssertEqual(response.payload.body.length, kPayloadSize);
                          [expectation fulfill];
                        }];
  GRPCCallOptions *options = rpc.options;
  options.responseSizeLimit = 6 * 1024 * 1024;

  [rpc start];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testPerformanceExample {
  // This is an example of a performance test case.
  [self measureBlock:^{
      // Put the code you want to measure the time of here.
  }];
}

@end
