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

#import <GRPCClient/GRPCCall.h>
#import <ProtoRPC/ProtoMethod.h>
#import <XCTest/XCTest.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#import "../version.h"

// The server address is derived from preprocessor macro, which is
// in turn derived from environment variable of the same name.
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *const kHostAddress = NSStringize(HOST_PORT_LOCAL);
static NSString *const kRemoteSSLHost = NSStringize(HOST_PORT_REMOTE);

// Package and service name of test server
static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";

static GRPCProtoMethod *kInexistentMethod;
static GRPCProtoMethod *kEmptyCallMethod;
static GRPCProtoMethod *kUnaryCallMethod;
static GRPCProtoMethod *kOutputStreamingCallMethod;
static GRPCProtoMethod *kFullDuplexCallMethod;

static const int kSimpleDataLength = 100;

static const NSTimeInterval kTestTimeout = 8;
static const NSTimeInterval kInvertedTimeout = 2;

// Reveal the _class ivar for testing access
@interface GRPCCall2 () {
 @public
  GRPCCall *_call;
}

@end

// Convenience class to use blocks as callbacks
@interface ClientTestsBlockCallbacks : NSObject<GRPCResponseHandler>

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback
                              writeDataCallback:(void (^)(void))writeDataCallback;

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback;

@end

@implementation ClientTestsBlockCallbacks {
  void (^_initialMetadataCallback)(NSDictionary *);
  void (^_messageCallback)(id);
  void (^_closeCallback)(NSDictionary *, NSError *);
  void (^_writeDataCallback)(void);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback
                              writeDataCallback:(void (^)(void))writeDataCallback {
  if ((self = [super init])) {
    _initialMetadataCallback = initialMetadataCallback;
    _messageCallback = messageCallback;
    _closeCallback = closeCallback;
    _writeDataCallback = writeDataCallback;
    _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback {
  return [self initWithInitialMetadataCallback:initialMetadataCallback
                               messageCallback:messageCallback
                                 closeCallback:closeCallback
                             writeDataCallback:nil];
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (self->_initialMetadataCallback) {
    self->_initialMetadataCallback(initialMetadata);
  }
}

- (void)didReceiveRawMessage:(GPBMessage *)message {
  if (self->_messageCallback) {
    self->_messageCallback(message);
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (self->_closeCallback) {
    self->_closeCallback(trailingMetadata, error);
  }
}

- (void)didWriteData {
  if (self->_writeDataCallback) {
    self->_writeDataCallback();
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end

@interface CallAPIv2Tests : XCTestCase<GRPCAuthorizationProtocol>

@end

@implementation CallAPIv2Tests

- (void)setUp {
  // This method isn't implemented by the remote server.
  kInexistentMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"Inexistent"];
  kEmptyCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"EmptyCall"];
  kUnaryCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"UnaryCall"];
  kOutputStreamingCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                                service:kService
                                                                 method:@"StreamingOutputCall"];
  kFullDuplexCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"FullDuplexCall"];
}

- (void)testMetadata {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kRemoteSSLHost
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  __block NSDictionary *init_md;
  __block NSDictionary *trailing_md;
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.oauth2AccessToken = @"bogusToken";
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[ClientTestsBlockCallbacks alloc]
                                 initWithInitialMetadataCallback:^(NSDictionary *initialMetadata) {
                                   init_md = initialMetadata;
                                 }
                                 messageCallback:^(id message) {
                                   XCTFail(@"Received unexpected response.");
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   trailing_md = trailingMetadata;
                                   if (error) {
                                     XCTAssertEqual(error.code, 16,
                                                    @"Finished with unexpected error: %@", error);
                                     XCTAssertEqualObjects(init_md,
                                                           error.userInfo[kGRPCHeadersKey]);
                                     XCTAssertEqualObjects(trailing_md,
                                                           error.userInfo[kGRPCTrailersKey]);
                                     NSString *challengeHeader = init_md[@"www-authenticate"];
                                     XCTAssertGreaterThan(challengeHeader.length, 0,
                                                          @"No challenge in response headers %@",
                                                          init_md);
                                     [expectation fulfill];
                                   }
                                 }]
                 callOptions:options];

  [call start];
  [call writeData:[request data]];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testUserAgentPrefix {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];
  __weak XCTestExpectation *recvInitialMd =
      [self expectationWithDescription:@"Did not receive initial md."];

  GRPCRequestOptions *request = [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                                                    path:kEmptyCallMethod.HTTPPath
                                                                  safety:GRPCCallSafetyDefault];
  NSDictionary *headers =
      [NSDictionary dictionaryWithObjectsAndKeys:@"", @"x-grpc-test-echo-useragent", nil];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.userAgentPrefix = @"Foo";
  options.initialMetadata = headers;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:request
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:^(
                                                                    NSDictionary *initialMetadata) {
               NSString *userAgent = initialMetadata[@"x-grpc-test-echo-useragent"];
               // Test the regex is correct
               NSString *expectedUserAgent = @"Foo grpc-objc/";
               expectedUserAgent =
                   [expectedUserAgent stringByAppendingString:GRPC_OBJC_VERSION_STRING];
               expectedUserAgent = [expectedUserAgent stringByAppendingString:@" grpc-c/"];
               expectedUserAgent =
                   [expectedUserAgent stringByAppendingString:GRPC_C_VERSION_STRING];
               expectedUserAgent = [expectedUserAgent stringByAppendingString:@" ("];
               expectedUserAgent = [expectedUserAgent stringByAppendingString:@GPR_PLATFORM_STRING];
               expectedUserAgent = [expectedUserAgent stringByAppendingString:@"; chttp2; "];
               expectedUserAgent = [expectedUserAgent
                   stringByAppendingString:[NSString stringWithUTF8String:grpc_g_stands_for()]];
               expectedUserAgent = [expectedUserAgent stringByAppendingString:@")"];
               XCTAssertEqualObjects(userAgent, expectedUserAgent);

               NSError *error = nil;
               // Change in format of user-agent field in a direction that does not match
               // the regex will likely cause problem for certain gRPC users. For details,
               // refer to internal doc https://goo.gl/c2diBc
               NSRegularExpression *regex = [NSRegularExpression
                   regularExpressionWithPattern:
                       @" grpc-[a-zA-Z0-9]+(-[a-zA-Z0-9]+)?/[^ ,]+( \\([^)]*\\))?"
                                        options:0
                                          error:&error];

               NSString *customUserAgent =
                   [regex stringByReplacingMatchesInString:userAgent
                                                   options:0
                                                     range:NSMakeRange(0, [userAgent length])
                                              withTemplate:@""];
               XCTAssertEqualObjects(customUserAgent, @"Foo");
               [recvInitialMd fulfill];
             }
                                 messageCallback:^(id message) {
                                   XCTAssertNotNil(message);
                                   XCTAssertEqual([message length], 0,
                                                  @"Non-empty response received: %@", message);
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   if (error) {
                                     XCTFail(@"Finished with unexpected error: %@", error);
                                   } else {
                                     [completion fulfill];
                                   }
                                 }]
                 callOptions:options];
  [call writeData:[NSData data]];
  [call start];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)getTokenWithHandler:(void (^)(NSString *token))handler {
  dispatch_queue_t queue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
  dispatch_sync(queue, ^{
    handler(@"test-access-token");
  });
}

- (void)testOAuthToken {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kEmptyCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.authTokenProvider = self;
  __block GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc]
                                 initWithInitialMetadataCallback:nil
                                                 messageCallback:nil
                                                   closeCallback:^(NSDictionary *trailingMetadata,
                                                                   NSError *error) {
                                                     [completion fulfill];
                                                   }]
                 callOptions:options];
  [call writeData:[NSData data]];
  [call start];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testResponseSizeLimitExceeded {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.responseSizeLimit = kSimpleDataLength;
  options.transportType = GRPCTransportTypeInsecure;

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.payload.body = [NSMutableData dataWithLength:options.responseSizeLimit];
  request.responseSize = (int32_t)(options.responseSizeLimit * 2);

  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc]
                                 initWithInitialMetadataCallback:nil
                                                 messageCallback:nil
                                                   closeCallback:^(NSDictionary *trailingMetadata,
                                                                   NSError *error) {
                                                     XCTAssertNotNil(error,
                                                                     @"Expecting non-nil error");
                                                     XCTAssertEqual(error.code,
                                                                    GRPCErrorCodeResourceExhausted);
                                                     [completion fulfill];
                                                   }]
                 callOptions:options];
  [call writeData:[request data]];
  [call start];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testIdempotentProtoRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Expected response."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = kSimpleDataLength;
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyIdempotentRequest];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id message) {
                                   NSData *data = (NSData *)message;
                                   XCTAssertNotNil(data, @"nil value received as response.");
                                   XCTAssertGreaterThan(data.length, 0,
                                                        @"Empty response received.");
                                   RMTSimpleResponse *responseProto =
                                       [RMTSimpleResponse parseFromData:data error:NULL];
                                   // We expect empty strings, not nil:
                                   XCTAssertNotNil(responseProto.username,
                                                   @"Response's username is nil.");
                                   XCTAssertNotNil(responseProto.oauthScope,
                                                   @"Response's OAuth scope is nil.");
                                   [response fulfill];
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   XCTAssertNil(error, @"Finished with unexpected error: %@",
                                                error);
                                   [completion fulfill];
                                 }]
                 callOptions:options];

  [call start];
  [call writeData:[request data]];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testTimeout {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.timeout = 0.001;
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kFullDuplexCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];

  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:
                 [[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                     messageCallback:^(NSData *data) {
                       XCTFail(@"Failure: response received; Expect: no response received.");
                     }
                     closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                       XCTAssertNotNil(error,
                                       @"Failure: no error received; Expect: receive "
                                       @"deadline exceeded.");
                       XCTAssertEqual(error.code, GRPCErrorCodeDeadlineExceeded);
                       [completion fulfill];
                     }]
                 callOptions:options];

  [call start];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testTimeoutBackoffWithTimeout:(double)timeout Backoff:(double)backoff {
  const double maxConnectTime = timeout > backoff ? timeout : backoff;
  const double kMargin = 0.1;

  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Timeout in a second."];
  NSString *const kDummyAddress = [NSString stringWithFormat:@"127.0.0.1:10000"];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kDummyAddress
                                          path:@"/dummy/path"
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.connectMinTimeout = timeout;
  options.connectInitialBackoff = backoff;
  options.connectMaxBackoff = 0;

  NSDate *startTime = [NSDate date];
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *data) {
                                   XCTFail(@"Received message. Should not reach here.");
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   XCTAssertNotNil(error,
                                                   @"Finished with no error; expecting error");
                                   XCTAssertLessThan(
                                       [[NSDate date] timeIntervalSinceDate:startTime],
                                       maxConnectTime + kMargin);
                                   [completion fulfill];
                                 }]
                 callOptions:options];

  [call start];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testTimeoutBackoff1 {
  [self testTimeoutBackoffWithTimeout:0.7 Backoff:0.4];
}

- (void)testTimeoutBackoff2 {
  [self testTimeoutBackoffWithTimeout:0.3 Backoff:0.8];
}

- (void)testCompression {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.expectCompressed = [RMTBoolValue message];
  request.expectCompressed.value = YES;
  request.responseCompressed = [RMTBoolValue message];
  request.expectCompressed.value = YES;
  request.responseSize = kSimpleDataLength;
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.compressionAlgorithm = GRPCCompressGzip;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *data) {
                                   NSError *error;
                                   RMTSimpleResponse *response =
                                       [RMTSimpleResponse parseFromData:data error:&error];
                                   XCTAssertNil(error, @"Error when parsing response: %@", error);
                                   XCTAssertEqual(response.payload.body.length, kSimpleDataLength);
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   XCTAssertNil(error, @"Received failure: %@", error);
                                   [completion fulfill];
                                 }]

                 callOptions:options];

  [call start];
  [call writeData:[request data]];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testFlowControlWrite {
  __weak XCTestExpectation *expectWriteData =
      [self expectationWithDescription:@"Reported write data"];

  RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = kSimpleDataLength;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.flowControlEnabled = YES;
  GRPCCall2 *call =
      [[GRPCCall2 alloc] initWithRequestOptions:callRequest
                                responseHandler:[[ClientTestsBlockCallbacks alloc]
                                                    initWithInitialMetadataCallback:nil
                                                                    messageCallback:nil
                                                                      closeCallback:nil
                                                                  writeDataCallback:^{
                                                                    [expectWriteData fulfill];
                                                                  }]
                                    callOptions:options];

  [call start];
  [call receiveNextMessages:1];
  [call writeData:[request data]];

  // Wait for 3 seconds and make sure we do not receive the response
  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];

  [call finish];
}

- (void)testFlowControlRead {
  __weak __block XCTestExpectation *expectBlockedMessage =
      [self expectationWithDescription:@"Message not delivered without recvNextMessage"];
  __weak __block XCTestExpectation *expectPassedMessage = nil;
  __weak __block XCTestExpectation *expectBlockedClose =
      [self expectationWithDescription:@"Call not closed with pending message"];
  __weak __block XCTestExpectation *expectPassedClose = nil;
  expectBlockedMessage.inverted = YES;
  expectBlockedClose.inverted = YES;

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = kSimpleDataLength;
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.flowControlEnabled = YES;
  __block int unblocked = NO;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *message) {
                                   if (!unblocked) {
                                     [expectBlockedMessage fulfill];
                                   } else {
                                     [expectPassedMessage fulfill];
                                   }
                                 }
                                 closeCallback:^(NSDictionary *trailers, NSError *error) {
                                   if (!unblocked) {
                                     [expectBlockedClose fulfill];
                                   } else {
                                     [expectPassedClose fulfill];
                                   }
                                 }]
                 callOptions:options];

  [call start];
  [call writeData:[request data]];
  [call finish];

  // Wait to make sure we do not receive the response
  [self waitForExpectationsWithTimeout:kInvertedTimeout handler:nil];

  expectPassedMessage =
      [self expectationWithDescription:@"Message delivered with receiveNextMessage"];
  expectPassedClose = [self expectationWithDescription:@"Close delivered after receiveNextMessage"];

  unblocked = YES;
  [call receiveNextMessages:1];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testFlowControlMultipleMessages {
  __weak XCTestExpectation *expectPassedMessage =
      [self expectationWithDescription:@"two messages delivered with receiveNextMessage"];
  expectPassedMessage.expectedFulfillmentCount = 2;
  __weak XCTestExpectation *expectBlockedMessage =
      [self expectationWithDescription:@"Message 3 not delivered"];
  expectBlockedMessage.inverted = YES;
  __weak XCTestExpectation *expectWriteTwice =
      [self expectationWithDescription:@"Write 2 messages done"];
  expectWriteTwice.expectedFulfillmentCount = 2;

  RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = kSimpleDataLength;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kHostAddress
                                          path:kFullDuplexCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.flowControlEnabled = YES;
  __block NSUInteger messageId = 0;
  __block GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *message) {
                                   if (messageId <= 1) {
                                     [expectPassedMessage fulfill];
                                   } else {
                                     [expectBlockedMessage fulfill];
                                   }
                                   messageId++;
                                 }
                                 closeCallback:nil
                                 writeDataCallback:^{
                                   [expectWriteTwice fulfill];
                                 }]
                 callOptions:options];

  [call receiveNextMessages:2];
  [call start];
  [call writeData:[request data]];
  [call writeData:[request data]];

  [self waitForExpectationsWithTimeout:kInvertedTimeout handler:nil];
}

- (void)testFlowControlReadReadyBeforeStart {
  __weak XCTestExpectation *expectPassedMessage =
      [self expectationWithDescription:@"Message delivered with receiveNextMessage"];
  __weak XCTestExpectation *expectPassedClose =
      [self expectationWithDescription:@"Close delivered with receiveNextMessage"];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = kSimpleDataLength;
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.flowControlEnabled = YES;
  __block BOOL closed = NO;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *message) {
                                   [expectPassedMessage fulfill];
                                   XCTAssertFalse(closed);
                                 }
                                 closeCallback:^(NSDictionary *ttrailers, NSError *error) {
                                   closed = YES;
                                   [expectPassedClose fulfill];
                                 }]
                 callOptions:options];

  [call receiveNextMessages:1];
  [call start];
  [call writeData:[request data]];
  [call finish];

  [self waitForExpectationsWithTimeout:kInvertedTimeout handler:nil];
}

- (void)testFlowControlReadReadyAfterStart {
  __weak XCTestExpectation *expectPassedMessage =
      [self expectationWithDescription:@"Message delivered with receiveNextMessage"];
  __weak XCTestExpectation *expectPassedClose =
      [self expectationWithDescription:@"Close delivered with receiveNextMessage"];

  RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
  RMTResponseParameters *parameters = [RMTResponseParameters message];
  parameters.size = kSimpleDataLength;
  [request.responseParametersArray addObject:parameters];
  request.payload.body = [NSMutableData dataWithLength:kSimpleDataLength];

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:(NSString *)kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  options.flowControlEnabled = YES;
  __block BOOL closed = NO;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *message) {
                                   [expectPassedMessage fulfill];
                                   XCTAssertFalse(closed);
                                 }
                                 closeCallback:^(NSDictionary *trailers, NSError *error) {
                                   closed = YES;
                                   [expectPassedClose fulfill];
                                 }]
                 callOptions:options];

  [call start];
  [call receiveNextMessages:1];
  [call writeData:[request data]];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testFlowControlReadNonBlockingFailure {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHostAddress
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.flowControlEnabled = YES;
  options.transportType = GRPCTransportTypeInsecure;

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.payload.body = [NSMutableData dataWithLength:options.responseSizeLimit];

  RMTEchoStatus *status = [RMTEchoStatus message];
  status.code = 2;
  status.message = @"test";
  request.responseStatus = status;

  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(NSData *data) {
                                   XCTFail(@"Received unexpected message");
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   XCTAssertNotNil(error, @"Expecting non-nil error");
                                   XCTAssertEqual(error.code, 2);
                                   [completion fulfill];
                                 }]
                 callOptions:options];
  [call writeData:[request data]];
  [call start];
  [call finish];

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

@end
