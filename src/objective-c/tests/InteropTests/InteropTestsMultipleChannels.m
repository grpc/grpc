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

#import <Cronet/Cronet.h>
#import <GRPCClient/GRPCCallOptions.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbrpc.h"

#import "../Common/TestUtils.h"
#import "../ConfigureCronet.h"
#import "InteropTestsBlockCallbacks.h"

#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)

static const NSTimeInterval TEST_TIMEOUT = 8000;

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

@interface InteropTestsMultipleChannels : XCTestCase

@end

dispatch_once_t initCronet;

@implementation InteropTestsMultipleChannels {
  RMTTestService *_remoteService;
  RMTTestService *_remoteCronetService;
  RMTTestService *_localCleartextService;
  RMTTestService *_localSSLService;
}

- (void)setUp {
  [super setUp];

  self.continueAfterFailure = NO;

  _remoteService = [RMTTestService serviceWithHost:GRPCGetRemoteInteropTestServerAddress()
                                       callOptions:nil];
  configureCronet(/*enable_netlog=*/false);

  // Default stack with remote host
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeCronet;
  // Cronet stack with remote host
  _remoteCronetService = [RMTTestService serviceWithHost:GRPCGetRemoteInteropTestServerAddress()
                                             callOptions:options];

  // Local stack with no SSL
  options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  _localCleartextService =
      [RMTTestService serviceWithHost:GRPCGetLocalInteropTestServerAddressPlainText()
                          callOptions:options];

  // Local stack with SSL
  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  NSString *certsPath = [bundle pathForResource:@"TestCertificates.bundle/test-certificates"
                                         ofType:@"pem"];
  NSError *error = nil;
  NSString *certs = [NSString stringWithContentsOfFile:certsPath
                                              encoding:NSUTF8StringEncoding
                                                 error:&error];
  XCTAssertNil(error);

  options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeChttp2BoringSSL;
  options.PEMRootCertificates = certs;
  options.hostNameOverride = @"foo.test.google.fr";
  _localSSLService = [RMTTestService serviceWithHost:GRPCGetLocalInteropTestServerAddressSSL()
                                         callOptions:options];
}

- (void)testEmptyUnaryRPC {
  __weak XCTestExpectation *expectRemote = [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCronetRemote =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCleartext =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectSSL = [self expectationWithDescription:@"Remote RPC finish"];

  GPBEmpty *request = [GPBEmpty message];

  void (^messageHandler)(id message) = ^(id message) {
    id expectedResponse = [GPBEmpty message];
    XCTAssertEqualObjects(message, expectedResponse);
  };

  GRPCUnaryProtoCall *callRemote = [_remoteService
      emptyCallWithMessage:request
           responseHandler:[[InteropTestsBlockCallbacks alloc]
                               initWithInitialMetadataCallback:nil
                                               messageCallback:messageHandler
                                                 closeCallback:^(NSDictionary *trailingMetadata,
                                                                 NSError *error) {
                                                   XCTAssertNil(error);
                                                   [expectRemote fulfill];
                                                 }
                                          writeMessageCallback:nil]
               callOptions:nil];
  GRPCUnaryProtoCall *callCronet = [_remoteCronetService
      emptyCallWithMessage:request
           responseHandler:[[InteropTestsBlockCallbacks alloc]
                               initWithInitialMetadataCallback:nil
                                               messageCallback:messageHandler
                                                 closeCallback:^(NSDictionary *trailingMetadata,
                                                                 NSError *error) {
                                                   XCTAssertNil(error);
                                                   [expectCronetRemote fulfill];
                                                 }
                                          writeMessageCallback:nil]
               callOptions:nil];
  GRPCUnaryProtoCall *callCleartext = [_localCleartextService
      emptyCallWithMessage:request
           responseHandler:[[InteropTestsBlockCallbacks alloc]
                               initWithInitialMetadataCallback:nil
                                               messageCallback:messageHandler
                                                 closeCallback:^(NSDictionary *trailingMetadata,
                                                                 NSError *error) {
                                                   XCTAssertNil(error);
                                                   [expectCleartext fulfill];
                                                 }
                                          writeMessageCallback:nil]
               callOptions:nil];
  GRPCUnaryProtoCall *callSSL = [_localSSLService
      emptyCallWithMessage:request
           responseHandler:[[InteropTestsBlockCallbacks alloc]
                               initWithInitialMetadataCallback:nil
                                               messageCallback:messageHandler
                                                 closeCallback:^(NSDictionary *trailingMetadata,
                                                                 NSError *error) {
                                                   XCTAssertNil(error);
                                                   [expectSSL fulfill];
                                                 }
                                          writeMessageCallback:nil]
               callOptions:nil];
  [callRemote start];
  [callCronet start];
  [callCleartext start];
  [callSSL start];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testFullDuplexRPC {
  __weak XCTestExpectation *expectRemote = [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCronetRemote =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCleartext =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectSSL = [self expectationWithDescription:@"Remote RPC finish"];

  NSArray *requestSizes = @[ @100, @101, @102, @103 ];
  NSArray *responseSizes = @[ @104, @105, @106, @107 ];
  XCTAssertEqual([requestSizes count], [responseSizes count]);
  NSUInteger kRounds = [requestSizes count];
  NSMutableArray<GRPCStreamingProtoCall *> *calls = [NSMutableArray arrayWithCapacity:4];

  NSMutableArray *requests = [NSMutableArray arrayWithCapacity:kRounds];
  NSMutableArray *responses = [NSMutableArray arrayWithCapacity:kRounds];
  for (int i = 0; i < kRounds; i++) {
    requests[i] = [RMTStreamingOutputCallRequest messageWithPayloadSize:requestSizes[i]
                                                  requestedResponseSize:responseSizes[i]];
    responses[i] = [RMTStreamingOutputCallResponse messageWithPayloadSize:responseSizes[i]];
  }

  __block NSMutableArray *steps = [NSMutableArray arrayWithCapacity:4];
  __block NSMutableArray *requestsBuffers = [NSMutableArray arrayWithCapacity:4];
  for (int i = 0; i < 4; i++) {
    steps[i] = [NSNumber numberWithUnsignedInteger:0];
    requestsBuffers[i] = [[GRXBufferedPipe alloc] init];
    [requestsBuffers[i] writeValue:requests[0]];
  }

  void (^handler)(NSUInteger index, id message) = ^(NSUInteger index, id message) {
    NSUInteger step = [steps[index] unsignedIntegerValue];
    step++;
    steps[index] = [NSNumber numberWithUnsignedInteger:step];
    if (step < kRounds) {
      [calls[index] writeMessage:requests[step]];
    } else {
      [calls[index] finish];
    }
  };

  calls[0] = [_remoteService
      fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                            initWithInitialMetadataCallback:nil
                                            messageCallback:^(id message) {
                                              handler(0, message);
                                            }
                                            closeCallback:^(NSDictionary *trailingMetadata,
                                                            NSError *error) {
                                              XCTAssertNil(error);
                                              [expectRemote fulfill];
                                            }
                                            writeMessageCallback:nil]
                            callOptions:nil];
  calls[1] = [_remoteCronetService
      fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                            initWithInitialMetadataCallback:nil
                                            messageCallback:^(id message) {
                                              handler(1, message);
                                            }
                                            closeCallback:^(NSDictionary *trailingMetadata,
                                                            NSError *error) {
                                              XCTAssertNil(error);
                                              [expectCronetRemote fulfill];
                                            }
                                            writeMessageCallback:nil]
                            callOptions:nil];
  calls[2] = [_localCleartextService
      fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                            initWithInitialMetadataCallback:nil
                                            messageCallback:^(id message) {
                                              handler(2, message);
                                            }
                                            closeCallback:^(NSDictionary *trailingMetadata,
                                                            NSError *error) {
                                              XCTAssertNil(error);
                                              [expectCleartext fulfill];
                                            }
                                            writeMessageCallback:nil]
                            callOptions:nil];
  calls[3] = [_localSSLService
      fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                            initWithInitialMetadataCallback:nil
                                            messageCallback:^(id message) {
                                              handler(3, message);
                                            }
                                            closeCallback:^(NSDictionary *trailingMetadata,
                                                            NSError *error) {
                                              XCTAssertNil(error);
                                              [expectSSL fulfill];
                                            }
                                            writeMessageCallback:nil]
                            callOptions:nil];
  for (int i = 0; i < 4; i++) {
    [calls[i] start];
    [calls[i] writeMessage:requests[0]];
  }

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

@end
