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

#import <XCTest/XCTest.h>
#import <grpc/grpc.h>
#import <grpc/support/port_platform.h>

#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+OAuth2.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoMethod.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"

#include <netinet/in.h>

#import "../version.h"

#define TEST_TIMEOUT 16

// The server address is derived from preprocessor macro, which is
// in turn derived from environment variable of the same name.
#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *const kHostAddress = NSStringize(HOST_PORT_LOCAL);
static NSString *const kPackage = @"grpc.testing";
static NSString *const kService = @"TestService";
static NSString *const kRemoteSSLHost = NSStringize(HOST_PORT_REMOTE);

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
  // Add a custom user agent prefix and suffix that will be used in test
  [GRPCCall setUserAgentPrefix:@"Foo" forHost:kHostAddress];
  [GRPCCall setUserAgentSuffix:@"Suffix" forHost:kHostAddress];
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
  kFullDuplexCallMethod = [[GRPCProtoMethod alloc] initWithPackage:kPackage
                                                           service:kService
                                                            method:@"FullDuplexCall"];
}

- (void)testConnectionToRemoteServer {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Server reachable."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kInexistentMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

- (void)testUserAgentPrefixAndSuffix {
  __weak XCTestExpectation *response =
      [self expectationWithDescription:@"Empty response received."];
  __weak XCTestExpectation *completion = [self expectationWithDescription:@"Empty RPC completed."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kHostAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  // Setting this special key in the header will cause the interop server to echo back the
  // user-agent value, which we confirm.
  call.requestHeaders[@"x-grpc-test-echo-useragent"] = @"";

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
        XCTAssertNotNil(value, @"nil value received as response.");
        XCTAssertEqual([value length], 0, @"Non-empty response received: %@", value);

        NSString *userAgent = call.responseHeaders[@"x-grpc-test-echo-useragent"];
        NSError *error = nil;

        // Test the regex is correct
        NSString *expectedUserAgent = @"Foo grpc-objc-cfstream/";
        expectedUserAgent = [expectedUserAgent stringByAppendingString:GRPC_OBJC_VERSION_STRING];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@" Suffix"];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@" grpc-c/"];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:GRPC_C_VERSION_STRING];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@" ("];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@GPR_PLATFORM_STRING];
        expectedUserAgent = [expectedUserAgent stringByAppendingString:@"; chttp2)"];
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

        NSArray *userAgentArray = [customUserAgent componentsSeparatedByString:@" "];
        XCTAssertEqual([userAgentArray count], 2);
        XCTAssertEqualObjects([userAgentArray objectAtIndex:0], @"Foo");
        XCTAssertEqualObjects([userAgentArray objectAtIndex:1], @"Suffix");
        [response fulfill];
      }
      completionHandler:^(NSError *errorOrNil) {
        XCTAssertNil(errorOrNil, @"Finished with unexpected error: %@", errorOrNil);
        [completion fulfill];
      }];

  [call startWithWriteable:responsesWriteable];

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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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
  GRXWriter *writer = [GRXWriter writerWithValue:[NSData data]];
  // Try to set parameters to nil for GRPCCall. This should cause an exception
  @try {
    (void)[[GRPCCall alloc] initWithHost:nil path:nil requestsWriter:writer];
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable1 = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable2 = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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
  NSString *const kPhonyAddress = [NSString stringWithFormat:@"localhost:%d", port];
  __weak XCTestExpectation *completion =
      [self expectationWithDescription:@"Received correct error code."];

  GRPCCall *call = [[GRPCCall alloc] initWithHost:kPhonyAddress
                                             path:kEmptyCallMethod.HTTPPath
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];

  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(NSData *value) {
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
  NSString *const kPhonyAddress = [NSString stringWithFormat:@"8.8.8.8:1"];
  [GRPCCall useInsecureConnectionsForHost:kPhonyAddress];
  [GRPCCall setMinConnectTimeout:timeout * 1000
                  initialBackoff:backoff * 1000
                      maxBackoff:0
                         forHost:kPhonyAddress];
  GRPCCall *call = [[GRPCCall alloc] initWithHost:kPhonyAddress
                                             path:@"/phonyPath"
                                   requestsWriter:[GRXWriter writerWithValue:[NSData data]]];
  NSDate *startTime = [NSDate date];
  id<GRXWriteable> responsesWriteable = [[GRXWriteable alloc]
      initWithValueHandler:^(id value) {
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

// The numbers of the following three tests are selected to be smaller than the default values of
// initial backoff (1s) and min_connect_timeout (20s), so that if they fail we know the default
// values fail to be overridden by the channel args.
- (void)testTimeoutBackoff1 {
  [self testTimeoutBackoffWithTimeout:0.7 Backoff:0.3];
}

- (void)testTimeoutBackoff2 {
  [self testTimeoutBackoffWithTimeout:0.3 Backoff:0.7];
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

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

@end
