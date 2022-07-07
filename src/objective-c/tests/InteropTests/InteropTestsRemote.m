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

#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <RxLibrary/GRXWriter+Immediate.h>

#import "../Common/GRPCBlockCallbackResponseHandler.h"

#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbrpc.h"

#import "InteropTests.h"

// Package and service name of test server
static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";

// The server address is derived from preprocessor macro, which is
// in turn derived from environment variable of the same name.
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *const kRemoteSSLHost = NSStringize(HOST_PORT_REMOTE);

// The Protocol Buffers encoding overhead of remote interop server. Acquired
// by experiment. Adjust this when server's proto file changes.
static int32_t kRemoteInteropServerOverhead = 12;

static const NSTimeInterval kTestTimeout = 8;

static GRPCProtoMethod *kUnaryCallMethod;

/** Tests in InteropTests.m, sending the RPCs to a remote SSL server. */
@interface InteropTestsRemote : InteropTests
@end

@implementation InteropTestsRemote

#pragma mark - InteropTests

+ (NSString *)host {
  return kRemoteSSLHost;
}

+ (NSString *)PEMRootCertificates {
  return nil;
}

+ (NSString *)hostNameOverride {
  return nil;
}

- (int32_t)encodingOverhead {
  return kRemoteInteropServerOverhead;  // bytes
}

+ (GRPCTransportType)transportType {
  return GRPCTransportTypeChttp2BoringSSL;
}

+ (BOOL)isRemoteTest {
  return YES;
}

#pragma mark - InteropTestsRemote tests

- (void)setUp {
  [super setUp];

  kUnaryCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                      service:kService
                                                       method:@"UnaryCall"];
}

- (void)testMetadataForV2Call {
  XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;

  GRPCRequestOptions *callRequest =
      [[GRPCRequestOptions alloc] initWithHost:[[self class] host]
                                          path:kUnaryCallMethod.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  __block NSDictionary *init_md;
  __block NSDictionary *trailing_md;
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.oauth2AccessToken = @"bogusToken";

  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:callRequest
             responseHandler:[[GRPCBlockCallbackResponseHandler alloc]
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

- (void)testMetadataForV1Call {
  XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRemoteSSLHost
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];

  call.oauth2AccessToken = @"bogusToken";

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

- (void)testErrorDebugInformation {
  XCTestExpectation *expectation = [self expectationWithDescription:@"RPC unauthorized."];

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.fillUsername = YES;
  request.fillOauthScope = YES;
  GRXWriter *requestsWriter = [GRXWriter writerWithValue:[request data]];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kRemoteSSLHost
                                             path:kUnaryCallMethod.HTTPPath
                                   requestsWriter:requestsWriter];

  call.oauth2AccessToken = @"bogusToken";

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  [self waitForExpectationsWithTimeout:kTestTimeout handler:nil];
}

@end
