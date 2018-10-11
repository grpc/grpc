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
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#import <RxLibrary/GRXBufferedPipe.h>

#define NSStringize_helper(x) #x
#define NSStringize(x) @NSStringize_helper(x)
static NSString *const kRemoteSSLHost = NSStringize(HOST_PORT_REMOTE);
static NSString *const kLocalSSLHost = NSStringize(HOST_PORT_LOCALSSL);
static NSString *const kLocalCleartextHost = NSStringize(HOST_PORT_LOCAL);

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

  // Default stack with remote host
  _remoteService = [RMTTestService serviceWithHost:kRemoteSSLHost];

  // Cronet stack with remote host
  _remoteCronetService = [RMTTestService serviceWithHost:kRemoteSSLHost];

  dispatch_once(&initCronet, ^{
    [Cronet setHttp2Enabled:YES];
    [Cronet start];
  });

  GRPCCallOptions *options = [[GRPCCallOptions alloc] init];
  options.transportType = GRPCTransportTypeCronet;
  options.cronetEngine = [Cronet getGlobalEngine];
  _remoteCronetService.options = options;

  // Local stack with no SSL
  _localCleartextService = [RMTTestService serviceWithHost:kLocalCleartextHost];
  options = [[GRPCCallOptions alloc] init];
  options.transportType = GRPCTransportTypeInsecure;
  _localCleartextService.options = options;

  // Local stack with SSL
  _localSSLService = [RMTTestService serviceWithHost:kLocalSSLHost];

  NSBundle *bundle = [NSBundle bundleForClass:self.class];
  NSString *certsPath =
      [bundle pathForResource:@"TestCertificates.bundle/test-certificates" ofType:@"pem"];
  NSError *error = nil;
  NSString *certs =
      [NSString stringWithContentsOfFile:certsPath encoding:NSUTF8StringEncoding error:&error];
  XCTAssertNil(error);

  options = [[GRPCCallOptions alloc] init];
  options.transportType = GRPCTransportTypeChttp2BoringSSL;
  options.PEMRootCertificates = certs;
  options.hostNameOverride = @"foo.test.google.fr";
  _localSSLService.options = options;
}

- (void)testEmptyUnaryRPC {
  __weak XCTestExpectation *expectRemote = [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCronetRemote =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectCleartext =
      [self expectationWithDescription:@"Remote RPC finish"];
  __weak XCTestExpectation *expectSSL = [self expectationWithDescription:@"Remote RPC finish"];

  GPBEmpty *request = [GPBEmpty message];

  void (^handler)(GPBEmpty *response, NSError *error) = ^(GPBEmpty *response, NSError *error) {
    XCTAssertNil(error, @"Finished with unexpected error: %@", error);

    id expectedResponse = [GPBEmpty message];
    XCTAssertEqualObjects(response, expectedResponse);
  };

  [_remoteService emptyCallWithRequest:request
                               handler:^(GPBEmpty *response, NSError *error) {
                                 handler(response, error);
                                 [expectRemote fulfill];
                               }];
  [_remoteCronetService emptyCallWithRequest:request
                                     handler:^(GPBEmpty *response, NSError *error) {
                                       handler(response, error);
                                       [expectCronetRemote fulfill];
                                     }];
  [_localCleartextService emptyCallWithRequest:request
                                       handler:^(GPBEmpty *response, NSError *error) {
                                         handler(response, error);
                                         [expectCleartext fulfill];
                                       }];
  [_localSSLService emptyCallWithRequest:request
                                 handler:^(GPBEmpty *response, NSError *error) {
                                   handler(response, error);
                                   [expectSSL fulfill];
                                 }];

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

  BOOL (^handler)(int, BOOL, RMTStreamingOutputCallResponse *, NSError *) =
      ^(int index, BOOL done, RMTStreamingOutputCallResponse *response, NSError *error) {
        XCTAssertNil(error, @"Finished with unexpected error: %@", error);
        XCTAssertTrue(done || response, @"Event handler called without an event.");
        if (response) {
          NSUInteger step = [steps[index] unsignedIntegerValue];
          XCTAssertLessThan(step, kRounds, @"More than %lu responses received.",
                            (unsigned long)kRounds);
          XCTAssertEqualObjects(response, responses[step]);
          step++;
          steps[index] = [NSNumber numberWithUnsignedInteger:step];
          GRXBufferedPipe *pipe = requestsBuffers[index];
          if (step < kRounds) {
            [pipe writeValue:requests[step]];
          } else {
            [pipe writesFinishedWithError:nil];
          }
        }
        if (done) {
          NSUInteger step = [steps[index] unsignedIntegerValue];
          XCTAssertEqual(step, kRounds, @"Received %lu responses instead of %lu.", step, kRounds);
          return YES;
        }
        return NO;
      };

  [_remoteService
      fullDuplexCallWithRequestsWriter:requestsBuffers[0]
                          eventHandler:^(BOOL done,
                                         RMTStreamingOutputCallResponse *_Nullable response,
                                         NSError *_Nullable error) {
                            if (handler(0, done, response, error)) {
                              [expectRemote fulfill];
                            }
                          }];
  [_remoteCronetService
      fullDuplexCallWithRequestsWriter:requestsBuffers[1]
                          eventHandler:^(BOOL done,
                                         RMTStreamingOutputCallResponse *_Nullable response,
                                         NSError *_Nullable error) {
                            if (handler(1, done, response, error)) {
                              [expectCronetRemote fulfill];
                            }
                          }];
  [_localCleartextService
      fullDuplexCallWithRequestsWriter:requestsBuffers[2]
                          eventHandler:^(BOOL done,
                                         RMTStreamingOutputCallResponse *_Nullable response,
                                         NSError *_Nullable error) {
                            if (handler(2, done, response, error)) {
                              [expectCleartext fulfill];
                            }
                          }];
  [_localSSLService
      fullDuplexCallWithRequestsWriter:requestsBuffers[3]
                          eventHandler:^(BOOL done,
                                         RMTStreamingOutputCallResponse *_Nullable response,
                                         NSError *_Nullable error) {
                            if (handler(3, done, response, error)) {
                              [expectSSL fulfill];
                            }
                          }];

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

@end
