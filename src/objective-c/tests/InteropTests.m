/*
 *
 * Copyright 2015 gRPC authors.
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

#import "InteropTests.h"

#include <grpc/status.h>

#import <Cronet/Cronet.h>
#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoRPC.h>
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <grpc/grpc.h>
#import <grpc/support/log.h>

#define TEST_TIMEOUT 32

extern const char *kCFStreamVarName;

// Convenience constructors for the generated proto messages:

@interface RMTStreamingOutputCallRequest (Constructors)
+ (instancetype)messageWithPayloadSize:(NSNumber *)payloadSize
                 requestedResponseSize:(NSNumber *)responseSize;
@end

@implementation RMTStreamingOutputCallRequest (Constructors)
+ (instancetype)messageWithPayloadSize:(NSNumber *)payloadSize
                 requestedResponseSize:(NSNumber *)responseSize {
  RMTStreamingOutputCallRequest *request = [self message];
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = responseSize.intValue;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:payloadSize.unsignedIntegerValue];
  return request;
}
@end

@interface RMTStreamingOutputCallResponse (Constructors)
+ (instancetype)messageWithPayloadSize:(NSNumber *)payloadSize;
@end

@implementation RMTStreamingOutputCallResponse (Constructors)
+ (instancetype)messageWithPayloadSize:(NSNumber *)payloadSize {
  RMTStreamingOutputCallResponse *response = [self message];
  response.payload.type = RMTPayloadType_Compressable;
  response.payload.body = [NSMutableData dataWithLength:payloadSize.unsignedIntegerValue];
  return response;
}
@end

BOOL isRemoteInteropTest(NSString *host) {
  return [host isEqualToString:@"grpc-test.sandbox.googleapis.com"];
}

#pragma mark Tests

@implementation InteropTests {
  RMTTestService *_service;
}

+ (NSString *)host {
  return nil;
}

// This number indicates how many bytes of overhead does Protocol Buffers encoding add onto the
// message. The number varies as different message.proto is used on different servers. The actual
// number for each interop server is overridden in corresponding derived test classes.
- (int32_t)encodingOverhead {
  return 0;
}

+ (void)setUp {
  NSLog(@"InteropTest Started, class: %@", [[self class] description]);
#ifdef GRPC_COMPILE_WITH_CRONET
  // Cronet setup
  [Cronet setHttp2Enabled:YES];
  [Cronet start];
  [GRPCCall useCronetWithEngine:[Cronet getGlobalEngine]];
#endif
#ifdef GRPC_CFSTREAM
  setenv(kCFStreamVarName, "1", 1);
#endif
}

- (void)setUp {
  self.continueAfterFailure = NO;

  [GRPCCall resetHostSettings];

  _service = self.class.host ? [RMTTestService serviceWithHost:self.class.host] : nil;
}

- (void)testEmptyUnaryRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyUnary"];

  GPBEmpty *request = [GPBEmpty message];

  [_service emptyCallWithRequest:request
                         handler:^(GPBEmpty *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                           id expectedResponse = [GPBEmpty message];
                           XCTAssertEqualObjects(response, expectedResponse);

                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testLargeUnaryRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseType = RMTPayloadType_Compressable;
  request.responseSize = 314159;
  request.payload.body = [NSMutableData dataWithLength:271828];

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                           RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                           expectedResponse.payload.type = RMTPayloadType_Compressable;
                           expectedResponse.payload.body = [NSMutableData dataWithLength:314159];
                           XCTAssertEqualObjects(response, expectedResponse);

                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testPacketCoalescing {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseType = RMTPayloadType_Compressable;
  request.responseSize = 10;
  request.payload.body = [NSMutableData dataWithLength:10];

  [GRPCCall enableOpBatchLog:YES];
  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                           RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                           expectedResponse.payload.type = RMTPayloadType_Compressable;
                           expectedResponse.payload.body = [NSMutableData dataWithLength:10];
                           XCTAssertEqualObjects(response, expectedResponse);

                           // The test is a success if there is a batch of exactly 3 ops
                           // (SEND_INITIAL_METADATA, SEND_MESSAGE, SEND_CLOSE_FROM_CLIENT). Without
                           // packet coalescing each batch of ops contains only one op.
                           NSArray *opBatches = [GRPCCall obtainAndCleanOpBatchLog];
                           const NSInteger kExpectedOpBatchSize = 3;
                           for (NSObject *o in opBatches) {
                             if ([o isKindOfClass:[NSArray class]]) {
                               NSArray *batch = (NSArray *)o;
                               if ([batch count] == kExpectedOpBatchSize) {
                                 [expectation fulfill];
                                 break;
                               }
                             }
                           }
                         }];

  [self waitForExpectationsWithTimeout:16 handler:nil];
  [GRPCCall enableOpBatchLog:NO];
}

- (void)test4MBResponsesAreAccepted {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"MaxResponseSize"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const int32_t kPayloadSize = 4 * 1024 * 1024 - self.encodingOverhead;  // 4MB - encoding overhead
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
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ResponseOverMaxSize"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const int32_t kPayloadSize = 4 * 1024 * 1024 - self.encodingOverhead + 1;  // 1B over max size
  request.responseSize = kPayloadSize;

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           // TODO(jcanizales): Catch the error and rethrow it with an actionable
                           // message:
                           // - Use +[GRPCCall setResponseSizeLimit:forHost:] to set a higher limit.
                           // - If you're developing the server, consider using response streaming,
                           // or let clients filter
                           //   responses by setting a google.protobuf.FieldMask in the request:
                           //   https://github.com/google/protobuf/blob/master/src/google/protobuf/field_mask.proto
                           XCTAssertEqualObjects(
                               error.localizedDescription,
                               @"Received message larger than max (4194305 vs. 4194304)");
                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testResponsesOver4MBAreAcceptedIfOptedIn {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation =
      [self expectationWithDescription:@"HigherResponseSizeLimit"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  const size_t kPayloadSize = 5 * 1024 * 1024;  // 5MB
  request.responseSize = kPayloadSize;

  [GRPCCall setResponseSizeLimit:6 * 1024 * 1024 forHost:self.class.host];

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                           XCTAssertEqual(response.payload.body.length, kPayloadSize);
                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testClientStreamingRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ClientStreaming"];

  RMTStreamingInputCallRequest *request1 = [RMTStreamingInputCallRequest message];
  request1.payload.body = [NSMutableData dataWithLength:27182];

  RMTStreamingInputCallRequest *request2 = [RMTStreamingInputCallRequest message];
  request2.payload.body = [NSMutableData dataWithLength:8];

  RMTStreamingInputCallRequest *request3 = [RMTStreamingInputCallRequest message];
  request3.payload.body = [NSMutableData dataWithLength:1828];

  RMTStreamingInputCallRequest *request4 = [RMTStreamingInputCallRequest message];
  request4.payload.body = [NSMutableData dataWithLength:45904];

  GRXWriter *writer = [GRXWriter writerWithContainer:@[ request1, request2, request3, request4 ]];

  [_service streamingInputCallWithRequestsWriter:writer
                                         handler:^(RMTStreamingInputCallResponse *response,
                                                   NSError *error) {
                                           XCTAssertNil(
                                               error, @"Finished with unexpected error: %@", error);

                                           RMTStreamingInputCallResponse *expectedResponse =
                                               [RMTStreamingInputCallResponse message];
                                           expectedResponse.aggregatedPayloadSize = 74922;
                                           XCTAssertEqualObjects(response, expectedResponse);

                                           [expectation fulfill];
                                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testServerStreamingRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ServerStreaming"];

  NSArray *expectedSizes = @[ @31415, @9, @2653, @58979 ];

  RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
  for (NSNumber *size in expectedSizes) {
    RMTResponseParameters *parameters = [RMTResponseParameters message];
    parameters.size = [size intValue];
    [request.responseParametersArray addObject:parameters];
  }

  __block int index = 0;
  [_service
      streamingOutputCallWithRequest:request
                        eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                       NSError *error) {
                          XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                          XCTAssertTrue(done || response,
                                        @"Event handler called without an event.");

                          if (response) {
                            XCTAssertLessThan(index, 4, @"More than 4 responses received.");
                            id expected = [RMTStreamingOutputCallResponse
                                messageWithPayloadSize:expectedSizes[index]];
                            XCTAssertEqualObjects(response, expected);
                            index += 1;
                          }

                          if (done) {
                            XCTAssertEqual(index, 4, @"Received %i responses instead of 4.", index);
                            [expectation fulfill];
                          }
                        }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testPingPongRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPong"];

  NSArray *requests = @[ @27182, @8, @1828, @45904 ];
  NSArray *responses = @[ @31415, @9, @2653, @58979 ];

  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  __block int index = 0;

  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                               requestedResponseSize:responses[index]];
  [requestsBuffer writeValue:request];

  [_service fullDuplexCallWithRequestsWriter:requestsBuffer
                                eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                               NSError *error) {
                                  XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                                  XCTAssertTrue(done || response,
                                                @"Event handler called without an event.");

                                  if (response) {
                                    XCTAssertLessThan(index, 4, @"More than 4 responses received.");
                                    id expected = [RMTStreamingOutputCallResponse
                                        messageWithPayloadSize:responses[index]];
                                    XCTAssertEqualObjects(response, expected);
                                    index += 1;
                                    if (index < 4) {
                                      id request = [RMTStreamingOutputCallRequest
                                          messageWithPayloadSize:requests[index]
                                           requestedResponseSize:responses[index]];
                                      [requestsBuffer writeValue:request];
                                    } else {
                                      [requestsBuffer writesFinishedWithError:nil];
                                    }
                                  }

                                  if (done) {
                                    XCTAssertEqual(index, 4, @"Received %i responses instead of 4.",
                                                   index);
                                    [expectation fulfill];
                                  }
                                }];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testEmptyStreamRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyStream"];
  [_service fullDuplexCallWithRequestsWriter:[GRXWriter emptyWriter]
                                eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                               NSError *error) {
                                  XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                                  XCTAssert(done, @"Unexpected response: %@", response);
                                  [expectation fulfill];
                                }];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testCancelAfterBeginRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"CancelAfterBegin"];

  // A buffered pipe to which we never write any value acts as a writer that just hangs.
  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  GRPCProtoCall *call = [_service
      RPCToStreamingInputCallWithRequestsWriter:requestsBuffer
                                        handler:^(RMTStreamingInputCallResponse *response,
                                                  NSError *error) {
                                          XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
                                          [expectation fulfill];
                                        }];
  XCTAssertEqual(call.state, GRXWriterStateNotStarted);

  [call start];
  XCTAssertEqual(call.state, GRXWriterStateStarted);

  [call cancel];
  XCTAssertEqual(call.state, GRXWriterStateFinished);

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testCancelAfterFirstResponseRPC {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation =
      [self expectationWithDescription:@"CancelAfterFirstResponse"];

  // A buffered pipe to which we write a single value but never close
  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  __block BOOL receivedResponse = NO;

  id request =
      [RMTStreamingOutputCallRequest messageWithPayloadSize:@21782 requestedResponseSize:@31415];

  [requestsBuffer writeValue:request];

  __block GRPCProtoCall *call = [_service
      RPCToFullDuplexCallWithRequestsWriter:requestsBuffer
                               eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                              NSError *error) {
                                 if (receivedResponse) {
                                   XCTAssert(done, @"Unexpected extra response %@", response);
                                   XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
                                   [expectation fulfill];
                                 } else {
                                   XCTAssertNil(error, @"Finished with unexpected error: %@",
                                                error);
                                   XCTAssertFalse(done, @"Finished without response");
                                   XCTAssertNotNil(response);
                                   receivedResponse = YES;
                                   [call cancel];
                                 }
                               }];
  [call start];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testRPCAfterClosingOpenConnections {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation =
      [self expectationWithDescription:@"RPC after closing connection"];

  GPBEmpty *request = [GPBEmpty message];

  [_service
      emptyCallWithRequest:request
                   handler:^(GPBEmpty *response, NSError *error) {
                     XCTAssertNil(error, @"First RPC finished with unexpected error: %@", error);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                     [GRPCCall closeOpenConnections];
#pragma clang diagnostic pop

                     [_service
                         emptyCallWithRequest:request
                                      handler:^(GPBEmpty *response, NSError *error) {
                                        XCTAssertNil(
                                            error, @"Second RPC finished with unexpected error: %@",
                                            error);
                                        [expectation fulfill];
                                      }];
                   }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testCompressedUnaryRPC {
  // This test needs to be disabled for remote test because interop server grpc-test
  // does not support compression.
  if (isRemoteInteropTest(self.class.host)) {
    return;
  }
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseType = RMTPayloadType_Compressable;
  request.responseSize = 314159;
  request.payload.body = [NSMutableData dataWithLength:271828];
  request.expectCompressed.value = YES;
  [GRPCCall setDefaultCompressMethod:GRPCCompressGzip forhost:self.class.host];

  [_service unaryCallWithRequest:request
                         handler:^(RMTSimpleResponse *response, NSError *error) {
                           XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                           RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                           expectedResponse.payload.type = RMTPayloadType_Compressable;
                           expectedResponse.payload.body = [NSMutableData dataWithLength:314159];
                           XCTAssertEqualObjects(response, expectedResponse);

                           [expectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

#ifndef GRPC_COMPILE_WITH_CRONET
- (void)testKeepalive {
  XCTAssertNotNil(self.class.host);
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Keepalive"];

  [GRPCCall setKeepaliveWithInterval:1500 timeout:0 forHost:self.class.host];

  NSArray *requests = @[ @27182, @8 ];
  NSArray *responses = @[ @31415, @9 ];

  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  __block int index = 0;

  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                               requestedResponseSize:responses[index]];
  [requestsBuffer writeValue:request];

  [_service
      fullDuplexCallWithRequestsWriter:requestsBuffer
                          eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                         NSError *error) {
                            if (index == 0) {
                              XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                              XCTAssertTrue(response, @"Event handler called without an event.");
                              XCTAssertFalse(done);
                              index++;
                            } else {
                              // Keepalive should kick after 1s elapsed and fails the call.
                              XCTAssertNotNil(error);
                              XCTAssertEqual(error.code, GRPC_STATUS_UNAVAILABLE);
                              XCTAssertEqualObjects(
                                  error.localizedDescription, @"keepalive watchdog timeout",
                                  @"Unexpected failure that is not keepalive watchdog timeout.");
                              XCTAssertTrue(done);
                              [expectation fulfill];
                            }
                          }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}
#endif

@end
