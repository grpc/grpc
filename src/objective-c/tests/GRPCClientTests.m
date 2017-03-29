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
#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+OAuth2.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RemoteTest/Messages.pbobjc.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Immediate.h>

#define TEST_TIMEOUT 16

static NSString * const kHostAddress = @"localhost:5050";
static NSString * const kPackage = @"grpc.testing";
static NSString * const kService = @"TestService";
static NSString * const kRemoteSSLHost = @"grpc-test.sandbox.googleapis.com";

static GRPCProtoMethod *kInexistentMethod;
static GRPCProtoMethod *kEmptyCallMethod;
static GRPCProtoMethod *kUnaryCallMethod;

/** Observer class for testing that responseMetadata is KVO-compliant */
@interface PassthroughObserver : NSObject
- (instancetype) initWithCallback:(void (^)(NSString*, id, NSDictionary*))callback
    NS_DESIGNATED_INITIALIZER;

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change
                       context:(void *)context;
@end

@implementation PassthroughObserver {
  void (^_callback)(NSString*, id, NSDictionary*);
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

# pragma mark Tests

/**
 * A few tests similar to InteropTests, but which use the generic gRPC client (GRPCCall) rather than
 * a generated proto library on top of it. Its RPCs are sent to a local cleartext server.
 *
 * TODO(jcanizales): Run them also against a local SSL server and against a remote server.
 */
@interface GRPCClientTests : XCTestCase
@end

@implementation GRPCClientTests

- (void)setUp {
  // Add a custom user agent prefix that will be used in test
  [GRPCCall setUserAgentPrefix:@"Foo" forHost:kHostAddress];
  // Register test server as non-SSL.
  [GRPCCall useInsecureConnectionsForHost:kHostAddress];

  // This method isn't implemented by the remote server.
  kInexistentMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                       service:kService
                                                        method:@"Inexistent"];
  kEmptyCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                      service:kService
                                                       method:@"EmptyCall"];
  kUnaryCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                      service:kService
                                                       method:@"UnaryCall"];
}

- (void)testConnectionToRemoteServer {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Server reachable."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kInexistentMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTFail(@"Received unexpected response: %@", value);
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNotNil(errorOrNil, @"Finished without error!");
    XCTAssertEqual(errorOrNil.code, 12, @"Finished with unexpected error: %@", errorOrNil);
    [expectation fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testEmptyRPC {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertGreaterThan(value.length, 0, @"Empty response received.");
    RMTSimpleResponse *responseProto = [RMTSimpleResponse parseFromData:value error:NULL];
    // We expect empty strings, not nil:
    XCTAssertNotNil(responseProto.username, @"Response's username is nil.");
    XCTAssertNotNil(responseProto.oauthScope, @"Response's OAuth scope is nil.");
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTFail(@"Received unexpected response: %@", value);
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNotNil(errorOrNil, @"Finished without error!");
    XCTAssertEqual(errorOrNil.code, 16, @"Finished with unexpected error: %@", errorOrNil);
    XCTAssertEqualObjects(call.responseHeaders, errorOrNil.userInfo[kGRPCHeadersKey],
                          @"Headers in the NSError object and call object differ.");
    XCTAssertEqualObjects(call.responseTrailers, errorOrNil.userInfo[kGRPCTrailersKey],
                          @"Trailers in the NSError object and call object differ.");
    NSString *challengeHeader = call.oauth2ChallengeHeader;
    XCTAssertGreaterThan(challengeHeader.length, 0,
                         @"No challenge in response headers %@", call.responseHeaders);
    [expectation fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testResponseMetadataKVO {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];
  __weak XCTestExpectation *metadata = [self expectationWithDescription:@"Metadata changed."];
  
  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  
  PassthroughObserver *observer = [[PassthroughObserver alloc] initWithCallback:^(NSString *keypath, id object, NSDictionary * change) {
    if ([keypath isEqual: @"responseHeaders"]) {
      [metadata fulfill];
    }
  }];
  
  [call addObserver:observer forKeyPath:@"responseHeaders" options:0 context:NULL];
  
  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
    [completion fulfill];
  }];
  
  [call startWithWriteable:responsesWriteable];
  
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testUserAgentPrefix {
  __weak XCTestExpectation *response = [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  // Setting this special key in the header will cause the interop server to echo back the
  // user-agent value, which we confirm.
  call.requestHeaders[@"x-grpc-test-echo-useragent"] = @"";

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);
    /* This test needs to be more clever in regards to changing the version of the core.
    XCTAssertEqualObjects(call.responseHeaders[@"x-grpc-test-echo-useragent"],
                          @"Foo grpc-objc/0.13.0 grpc-c/0.14.0-dev (ios)",
                          @"Did not receive expected user agent %@",
                          call.responseHeaders[@"x-grpc-test-echo-useragent"]);
    */
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
    [completion fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

// TODO(makarandd): Move to a different file that contains only unit tests
- (void)testExceptions {
  // Try to set parameters to nil for GRPCCall. This should cause an exception
  @try {
    (void)[[GRPCCall alloc] initWithHost:nil
                                    path:nil
                          requestsWriter:nil];
    XCTFail(@"Did not receive an exception when parameters are nil");
  } @catch(NSException *theException) {
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
  } @catch(NSException *theException) {
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
  [GRPCCall setCallSafety:GRPCCallSafetyIdempotentRequest host:kHostAddress path:kUnaryCallMethod.HTTPPath];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
    XCTAssertNotNil(value, @"nil value received as response.");
    XCTAssertGreaterThan(value.length, 0, @"Empty response received.");
    RMTSimpleResponse *responseProto = [RMTSimpleResponse parseFromData:value error:NULL];
    // We expect empty strings, not nil:
    XCTAssertNotNil(responseProto.username, @"Response's username is nil.");
    XCTAssertNotNil(responseProto.oauthScope, @"Response's OAuth scope is nil.");
    [response fulfill];
  } completionHandler:^(NSError *errorOrNil) {
    XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
    [completion fulfill];
  }];

  [call startWithWriteable:responsesWriteable];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

@end
