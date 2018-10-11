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

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>
#import <grpc/grpc.h>

#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+OAuth2.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RemoteTest/Messages.pbobjc.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Immediate.h>

#include <netinet/in.h>

#import "version.h"

#define TEST_TIMEOUT 16

static NSString *const kHostAddress = @"localhost:5050";
static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";
static NSString *const kRemoteSSLHost = @"grpc-test.sandbox.googleapis.com";

static GRPCProtoMethod *kInexistentMethod;
static GRPCProtoMethod *kEmptyCallMethod;
static GRPCProtoMethod *kUnaryCallMethod;
static GRPCProtoMethod *kFullDuplexCallMethod;

/** Observer class for testing that responseMetadata is KVO-compliant */
@interface PassthroughObserver : NSObject
- (instancetype)initWithCallback:(void (^)(NSString *, id, NSDictionary *))callback
    NS_DESIGNATED_INITIALIZER;

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context;
@end

@implementation PassthroughObserver {
  void (^_callback)(NSString *, id, NSDictionary *);
}

- (instancetype)init {
  return [self initWithCallback:nil];
}

- (instancetype)initWithCallback:(void (^)(NSString *, id, NSDictionary *))callback {
  if (!callback) {
    return nil;
  }
  if ((self = [super init])) {
    _callback = callback;
  }
  return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
  _callback(keyPath, object, change);
  [object removeObserver:self forKeyPath:keyPath];
}

@end

// Convenience class to use blocks as callbacks
@interface ClientTestsBlockCallbacks : NSObject<GRPCResponseHandler>

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback;

@end

@implementation ClientTestsBlockCallbacks {
  void (^_initialMetadataCallback)(NSDictionary *);
  void (^_messageCallback)(id);
  void (^_closeCallback)(NSDictionary *, NSError *);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback {
  if ((self = [super init])) {
    _initialMetadataCallback = initialMetadataCallback;
    _messageCallback = messageCallback;
    _closeCallback = closeCallback;
    _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (void)receivedInitialMetadata:(NSDictionary *)initialMetadata {
  if (_initialMetadataCallback) {
    _initialMetadataCallback(initialMetadata);
  }
}

- (void)receivedMessage:(id)message {
  if (_messageCallback) {
    _messageCallback(message);
  }
}

- (void)closedWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (_closeCallback) {
    _closeCallback(trailingMetadata, error);
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end

#pragma mark Tests

/**
 * A few tests similar to InteropTests, but which use the generic gRPC client (GRPCCall) rather than
 * a generated proto library on top of it. Its RPCs are sent to a local cleartext server.
 *
 * TODO(jcanizales): Run them also against a local SSL server and against a remote server.
 */
@interface GRPCClientTests : XCTestCase
@end

@implementation GRPCClientTests

+ (void)setUp {
  NSLog(@"GRPCClientTests Started");
}

- (void)setUp {
  // Add a custom user agent prefix that will be used in test
  [GRPCCall setUserAgentPrefix:@"Foo" forHost:kHostAddress];
  // Register test server as non-SSL.
  [GRPCCall useInsecureConnectionsForHost:kHostAddress];

  // This method isn't implemented by the remote server.
  kInexistentMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"Inexistent"];
  kEmptyCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"EmptyCall"];
  kUnaryCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"UnaryCall"];
  kFullDuplexCallMethod =
      [[GRPCProtoMethod alloc] initWithPackage:kPackage service:kService method:@"FullDuplexCall"];
}

- (void)testConnectionToRemoteServer {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Server reachable."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kInexistentMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTFail(@"Received unexpected response: %@", value);
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNotNil(errorOrNil, @"Finished without error!");
            XCTAssertEqual(errorOrNil.code, 12, @"Finished with unexpected error: %@", errorOrNil);
            [expectation fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testEmptyRPC {
  __weak XCTestExpectation *response =
      [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testSimpleProtoRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Expected response."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 100;
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertGreaterThan(value.length, 0, @"Empty response received.");
        RMTSimpleResponse *responseProto = [RMTSimpleResponse parseFromData:value error:NULL];
        // We expect empty strings, not nil:
        XCTAssertNotNil(responseProto.username, @"Response's username is nil.");
        XCTAssertNotNil(responseProto.oauthScope, @"Response's OAuth scope is nil.");
        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testMetadata {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRemoteSSLHost
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];

  call.oauth2AccessToken = @"bogusToken";

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTFail(@"Received unexpected response: %@", value);
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNotNil(errorOrNil, @"Finished without error!");
            XCTAssertEqual(errorOrNil.code, 16, @"Finished with unexpected error: %@", errorOrNil);
            XCTAssertEqualObjects(call.responseHeaders, errorOrNil.userInfo[kGRPCHeadersKey],
                                  @"Headers in the NSError object and call object differ.");
            XCTAssertEqualObjects(call.responseTrailers, errorOrNil.userInfo[kGRPCTrailersKey],
                                  @"Trailers in the NSError object and call object differ.");
            NSString *challengeHeader = call.oauth2ChallengeHeader;
            XCTAssertGreaterThan(challengeHeader.length, 0, @"No challenge in response headers %@",
                                 call.responseHeaders);
            [expectation fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testMetadataWithV2API {
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testResponseMetadataKVO {
  __weak XCTestExpectation *response =
      [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];
  __weak XCTestExpectation *metadata = [self expectationWithDescription:@"Metadata changed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  PassthroughObserver *observer = [[PassthroughObserver alloc]
      initWithCallback:^(NSString *keypath, id object, NSDictionary *change) {
        if ([keypath isEqual:@"responseHeaders"]) {
          [metadata fulfill];
        }
      }];

  [call addObserver:observer forKeyPath:@"responseHeaders" options:0 context:NULL];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testUserAgentPrefix {
  __weak XCTestExpectation *response =
      [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  // Setting this special key in the header will cause the interop server to echo back the
  // user-agent value, which we confirm.
  call.requestHeaders[@"x-grpc-test-echo-useragent"] = @"";

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);

        NSString *userAgent = call.responseHeaders[@"x-grpc-test-echo-useragent"];
        NSError *error = nil;

        // Test the regex is correct
        NSString *expectedUserAgent = @"Foo grpc-objc/";
        expectedUserAgent = [expectedUserAgent stringByAppendingString:GRPC_OBJC_VERSION_STRING];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@" grpc-c/"];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:GRPC_C_VERSION_STRING];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@" (ios; chttp2; "];
        expectedUserAgent = [expectedUserAgent
            stringByAppendingString:[NSString stringWithUTF8String:grpc_g_stands_for()]];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@")"];
        XCTAssertEqualObjects(userAgent, expectedUserAgent);

        // Change in format of user-agent field in a direction that does not match the regex will
        // likely cause problem for certain gRPC users. For details, refer to internal doc
        // https://goo.gl/c2diBc
        NSRegularExpression *regex = [NSRegularExpression
            regularExpressionWithPattern:@" grpc-[a-zA-Z0-9]+(-[a-zA-Z0-9]+)?/[^ ,]+( \\([^)]*\\))?"
                                 options:0
                                   error:&error];
        NSString *customUserAgent =
            [regex stringByReplacingMatchesInString:userAgent
                                            options:0
                                              range:NSMakeRange(0, [userAgent length])
                                       withTemplate:@""];
        XCTAssertEqualObjects(customUserAgent, @"Foo");

        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testUserAgentPrefixWithV2API {
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
                       expectedUserAgent =
                           [expectedUserAgent stringByAppendingString:@" (ios; chttp2; "];
                       expectedUserAgent = [expectedUserAgent
                           stringByAppendingString:[NSString
                                                       stringWithUTF8String:grpc_g_stands_for()]];
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

                       NSString *customUserAgent = [regex
                           stringByReplacingMatchesInString:userAgent
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testTrailers {
  __weak XCTestExpectation *response =
      [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  // Setting this special key in the header will cause the interop server to echo back the
  // trailer data.
  const unsigned char raw_bytes[] = {1, 2, 3, 4};
  NSData *trailer_data = [NSData dataWithBytes:raw_bytes length:sizeof(raw_bytes)];
  call.requestHeaders[@"x-grpc-test-echo-trailing-bin"] = trailer_data;

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            XCTAssertEqualObjects((NSData *)call.responseTrailers[@"x-grpc-test-echo-trailing-bin"],
                                  trailer_data, @"Did not receive expected trailer");
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

// TODO(makarandd): Move to a different file that contains only unit tests
- (void)testExceptions {
  // Try to set parameters to nil for GRPCCall. This should cause an exception
  @try {
    (void)[[GRPCCall alloc] initWithHost:nil path:nil requestsWriter:nil];
    XCTFail(@"Did not receive an exception when parameters are nil");
  } @catch (NSException *theException) {
    NSLog(@"Received exception as expected: %@", theException.name);
  }

  // Set state to Finished by force
  GRXWriter *requestsWriter = [GRXWriter emptyWriter];
  [requestsWriter finishWithError:nil];
  @try {
    (void)[[GRPCCall alloc] initWithHost:kHostAddress
                                    path:kUnaryCallMethod.HTTPPath
                          requestsWriter:requestsWriter];
    XCTFail(@"Did not receive an exception when GRXWriter has incorrect state.");
  } @catch (NSException *theException) {
    NSLog(@"Received exception as expected: %@", theException.name);
  }
}

- (void)testIdempotentProtoRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Expected response."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 100;
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];
  [GRPCCall setCallSafety:GRPCCallSafetyIdempotentRequest
                     host:kHostAddress
                     path:kUnaryCallMethod.HTTPPath];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertGreaterThan(value.length, 0, @"Empty response received.");
        RMTSimpleResponse *responseProto = [RMTSimpleResponse parseFromData:value error:NULL];
        // We expect empty strings, not nil:
        XCTAssertNotNil(responseProto.username, @"Response's username is nil.");
        XCTAssertNotNil(responseProto.oauthScope, @"Response's OAuth scope is nil.");
        [response fulfill];
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testIdempotentProtoRPCWithV2API {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Expected response."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 100;
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testAlternateDispatchQueue {
  const int32_t kPayloadSize = 100;
  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = kPayloadSize;

  __weak XCTestExpectation *expectation1 =
      [self expectationWithDescription:@"AlternateDispatchQueue1"];

  // Use default (main) dispatch queue
  NSString *main_queue_label =
      [NSString stringWithUTF8String:dispatch_queue_get_label(dispatch_get_main_queue())];

  GRXWriter *requestsWriter1 = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call1 = [[GRPCCall alloc] initWithHost:kHostAddress
                                              path:kUnaryCallMethod.HTTPPath
                                    requestsWriter:requestsWriter1];

  id<GRXWriteable> responsesWriteable1 =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        NSString *label =
            [NSString stringWithUTF8String:dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL)];
        XCTAssert([label isEqualToString:main_queue_label]);

        [expectation1 fulfill];
      }
                               completionHandler:^(NSError *errorOrNil){
                               }];

  [call1 startWithWriteable:responsesWriteable1];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];

  // Use a custom  queue
  __weak XCTestExpectation *expectation2 =
      [self expectationWithDescription:@"AlternateDispatchQueue2"];

  NSString *queue_label = @"test.queue1";
  dispatch_queue_t queue = dispatch_queue_create([queue_label UTF8String], DISPATCH_QUEUE_SERIAL);

  GRXWriter *requestsWriter2 = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call2 = [[GRPCCall alloc] initWithHost:kHostAddress
                                              path:kUnaryCallMethod.HTTPPath
                                    requestsWriter:requestsWriter2];

  [call2 setResponseDispatchQueue:queue];

  id<GRXWriteable> responsesWriteable2 =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        NSString *label =
            [NSString stringWithUTF8String:dispatch_queue_get_label(DISPATCH_CURRENT_QUEUE_LABEL)];
        XCTAssert([label isEqualToString:queue_label]);

        [expectation2 fulfill];
      }
                               completionHandler:^(NSError *errorOrNil){
                               }];

  [call2 startWithWriteable:responsesWriteable2];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testTimeout {
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"RPC completed."];

  GRXBufferedPipe *pipe = [GRXBufferedPipe pipe];
  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kFullDuplexCallMethod.HTTPPath
                                   requestsWriter:pipe];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTAssert(0, @"Failure: response received; Expect: no response received.");
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNotNil(errorOrNil,
                            @"Failure: no error received; Expect: receive deadline exceeded.");
            XCTAssertEqual(errorOrNil.code, GRPCErrorCodeDeadlineExceeded);
            [completion fulfill];
          }];

  call.timeout = 0.001;
  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testTimeoutWithV2API {
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
                             messageCallback:^(id data) {
                               XCTFail(
                                   @"Failure: response received; Expect: no response received.");
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (int)findFreePort {
  struct sockaddr_in addr;
  unsigned int addr_len = sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  XCTAssertEqual(bind(fd, (struct sockaddr *)&addr, sizeof(addr)), 0);
  XCTAssertEqual(getsockname(fd, (struct sockaddr *)&addr, &addr_len), 0);
  XCTAssertEqual(addr_len, sizeof(addr));
  close(fd);
  return addr.sin_port;
}

- (void)testErrorCode {
  int port = [self findFreePort];
  NSString *const kDummyAddress = [NSString stringWithFormat:@"localhost:%d", port];
  __weak XCTestExpectation *completion =
      [self expectationWithDescription:@"Received correct error code."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kDummyAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        // Should not reach here
        XCTAssert(NO);
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNotNil(errorOrNil, @"Finished with no error");
            XCTAssertEqual(errorOrNil.code, GRPC_STATUS_UNAVAILABLE);
            [completion fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testTimeoutBackoffWithTimeout:(double)timeout Backoff:(double)backoff {
  const double maxConnectTime = timeout > backoff ? timeout : backoff;
  const double kMargin = 0.1;

  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Timeout in a second."];
  NSString *const kDummyAddress = [NSString stringWithFormat:@"8.8.8.8:1"];
  GRPCCall *call = [[GRPCCall alloc] initWithHost:kDummyAddress
                                             path:@""
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  [GRPCCall setMinConnectTimeout:timeout * 1000
                  initialBackoff:backoff * 1000
                      maxBackoff:0
                         forHost:kDummyAddress];
  NSDate *startTime = [NSDate date];
  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(id value) {
    XCTAssert(NO, @"Received message. Should not reach here");
  }
      completionHandler:^(NSError *errorOrNil) {
        XCTAssertNotNil(errorOrNil, @"Finished with no error");
        // The call must fail before maxConnectTime. However there is no lower bound on the time
        // taken for connection. A shorter time happens when connection is actively refused
        // by 8.8.8.8:1 before maxConnectTime elapsed.
        XCTAssertLessThan([[NSDate date] timeIntervalSinceDate:startTime],
                          maxConnectTime + kMargin);
        [completion fulfill];
      }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testTimeoutBackoffWithOptionsWithTimeout:(double)timeout Backoff:(double)backoff {
  const double maxConnectTime = timeout > backoff ? timeout : backoff;
  const double kMargin = 0.1;

  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Timeout in a second."];
  NSString *const kDummyAddress = [NSString stringWithFormat:@"127.0.0.1:10000"];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kDummyAddress path:@"/dummy/path" safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.connectMinTimeout = timeout;
  options.connectInitialBackoff = backoff;
  options.connectMaxBackoff = 0;

  NSDate *startTime = [NSDate date];
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
                     responseHandler:[[ClientTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id data) {
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

// The numbers of the following three tests are selected to be smaller than the default values of
// initial backoff (1s) and min_connect_timeout (20s), so that if they fail we know the default
// values fail to be overridden by the channel args.
- (void)testTimeoutBackoff1 {
  [self testTimeoutBackoffWithTimeout:0.7 Backoff:0.3];
  [self testTimeoutBackoffWithOptionsWithTimeout:0.7 Backoff:0.4];
}

- (void)testTimeoutBackoff2 {
  [self testTimeoutBackoffWithTimeout:0.3 Backoff:0.7];
  [self testTimeoutBackoffWithOptionsWithTimeout:0.3 Backoff:0.8];
}

- (void)testErrorDebugInformation {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRemoteSSLHost
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];

  call.oauth2AccessToken = @"bogusToken";

  id<GRXWriteable> responsesWriteable =
      [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
        XCTFail(@"Received unexpected response: %@", value);
      }
          completionHandler:^(NSError *errorOrNil) {
            XCTAssertNotNil(errorOrNil, @"Finished without error!");
            NSDictionary *userInfo = errorOrNil.userInfo;
            NSString *debugInformation = userInfo[NSDebugDescriptionErrorKey];
            XCTAssertNotNil(debugInformation);
            XCTAssertNotEqual([debugInformation length], 0);
            NSString *challengeHeader = call.oauth2ChallengeHeader;
            XCTAssertGreaterThan(challengeHeader.length, 0, @"No challenge in response headers %@",
                                 call.responseHeaders);
            [expectation fulfill];
          }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

@end
