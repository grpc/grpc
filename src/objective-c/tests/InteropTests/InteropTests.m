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

#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+Cronet.h>
#import <GRPCClient/GRPCCall+Interceptor.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/GRPCInterceptor.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoRPC.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <grpc/grpc.h>
#import <grpc/support/log.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbrpc.h"

#import "../Common/TestUtils.h"
#import "InteropTestsBlockCallbacks.h"

#define SMALL_PAYLOAD_SIZE 10
#define LARGE_REQUEST_PAYLOAD_SIZE 271828
#define LARGE_RESPONSE_PAYLOAD_SIZE 314159

static const int kTestRetries = 3;
extern const char *kCFStreamVarName;

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

@interface DefaultInterceptorFactory : NSObject <GRPCInterceptorFactory>

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager;

@end

@implementation DefaultInterceptorFactory

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager {
  dispatch_queue_t queue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
  return [[GRPCInterceptor alloc] initWithInterceptorManager:interceptorManager
                                               dispatchQueue:queue];
}

@end

@interface HookInterceptorFactory : NSObject <GRPCInterceptorFactory>

- (instancetype)
      initWithDispatchQueue:(dispatch_queue_t)dispatchQueue
                  startHook:(void (^)(GRPCRequestOptions *requestOptions,
                                      GRPCCallOptions *callOptions,
                                      GRPCInterceptorManager *manager))startHook
              writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                 finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
    receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                      GRPCInterceptorManager *manager))receiveNextMessagesHook
         responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                      GRPCInterceptorManager *manager))responseHeaderHook
           responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
          responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                      GRPCInterceptorManager *manager))responseCloseHook
           didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook;

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager;

@end

@interface HookInterceptor : GRPCInterceptor

- (instancetype)
    initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                 dispatchQueue:(dispatch_queue_t)dispatchQueue
                     startHook:(void (^)(GRPCRequestOptions *requestOptions,
                                         GRPCCallOptions *callOptions,
                                         GRPCInterceptorManager *manager))startHook
                 writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                    finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
       receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                         GRPCInterceptorManager *manager))receiveNextMessagesHook
            responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                         GRPCInterceptorManager *manager))responseHeaderHook
              responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
             responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                         GRPCInterceptorManager *manager))responseCloseHook
              didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook;

@end

@implementation HookInterceptorFactory {
 @protected
  void (^_startHook)(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                     GRPCInterceptorManager *manager);
  void (^_writeDataHook)(id data, GRPCInterceptorManager *manager);
  void (^_finishHook)(GRPCInterceptorManager *manager);
  void (^_receiveNextMessagesHook)(NSUInteger numberOfMessages, GRPCInterceptorManager *manager);
  void (^_responseHeaderHook)(NSDictionary *initialMetadata, GRPCInterceptorManager *manager);
  void (^_responseDataHook)(id data, GRPCInterceptorManager *manager);
  void (^_responseCloseHook)(NSDictionary *trailingMetadata, NSError *error,
                             GRPCInterceptorManager *manager);
  void (^_didWriteDataHook)(GRPCInterceptorManager *manager);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)
      initWithDispatchQueue:(dispatch_queue_t)dispatchQueue
                  startHook:(void (^)(GRPCRequestOptions *requestOptions,
                                      GRPCCallOptions *callOptions,
                                      GRPCInterceptorManager *manager))startHook
              writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                 finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
    receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                      GRPCInterceptorManager *manager))receiveNextMessagesHook
         responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                      GRPCInterceptorManager *manager))responseHeaderHook
           responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
          responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                      GRPCInterceptorManager *manager))responseCloseHook
           didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook {
  if ((self = [super init])) {
    _dispatchQueue = dispatchQueue;
    _startHook = startHook;
    _writeDataHook = writeDataHook;
    _finishHook = finishHook;
    _receiveNextMessagesHook = receiveNextMessagesHook;
    _responseHeaderHook = responseHeaderHook;
    _responseDataHook = responseDataHook;
    _responseCloseHook = responseCloseHook;
    _didWriteDataHook = didWriteDataHook;
  }
  return self;
}

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager {
  return [[HookInterceptor alloc] initWithInterceptorManager:interceptorManager
                                               dispatchQueue:_dispatchQueue
                                                   startHook:_startHook
                                               writeDataHook:_writeDataHook
                                                  finishHook:_finishHook
                                     receiveNextMessagesHook:_receiveNextMessagesHook
                                          responseHeaderHook:_responseHeaderHook
                                            responseDataHook:_responseDataHook
                                           responseCloseHook:_responseCloseHook
                                            didWriteDataHook:_didWriteDataHook];
}

@end

@implementation HookInterceptor {
  void (^_startHook)(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                     GRPCInterceptorManager *manager);
  void (^_writeDataHook)(id data, GRPCInterceptorManager *manager);
  void (^_finishHook)(GRPCInterceptorManager *manager);
  void (^_receiveNextMessagesHook)(NSUInteger numberOfMessages, GRPCInterceptorManager *manager);
  void (^_responseHeaderHook)(NSDictionary *initialMetadata, GRPCInterceptorManager *manager);
  void (^_responseDataHook)(id data, GRPCInterceptorManager *manager);
  void (^_responseCloseHook)(NSDictionary *trailingMetadata, NSError *error,
                             GRPCInterceptorManager *manager);
  void (^_didWriteDataHook)(GRPCInterceptorManager *manager);
  GRPCInterceptorManager *_manager;
  dispatch_queue_t _dispatchQueue;
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

- (instancetype)
    initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                 dispatchQueue:(dispatch_queue_t)dispatchQueue
                     startHook:(void (^)(GRPCRequestOptions *requestOptions,
                                         GRPCCallOptions *callOptions,
                                         GRPCInterceptorManager *manager))startHook
                 writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                    finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
       receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                         GRPCInterceptorManager *manager))receiveNextMessagesHook
            responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                         GRPCInterceptorManager *manager))responseHeaderHook
              responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
             responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                         GRPCInterceptorManager *manager))responseCloseHook
              didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook {
  if ((self = [super initWithInterceptorManager:interceptorManager dispatchQueue:dispatchQueue])) {
    _startHook = startHook;
    _writeDataHook = writeDataHook;
    _finishHook = finishHook;
    _receiveNextMessagesHook = receiveNextMessagesHook;
    _responseHeaderHook = responseHeaderHook;
    _responseDataHook = responseDataHook;
    _responseCloseHook = responseCloseHook;
    _didWriteDataHook = didWriteDataHook;
    _dispatchQueue = dispatchQueue;
    _manager = interceptorManager;
  }
  return self;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  if (_startHook) {
    _startHook(requestOptions, callOptions, _manager);
  }
}

- (void)writeData:(id)data {
  if (_writeDataHook) {
    _writeDataHook(data, _manager);
  }
}

- (void)finish {
  if (_finishHook) {
    _finishHook(_manager);
  }
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  if (_receiveNextMessagesHook) {
    _receiveNextMessagesHook(numberOfMessages, _manager);
  }
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (_responseHeaderHook) {
    _responseHeaderHook(initialMetadata, _manager);
  }
}

- (void)didReceiveData:(id)data {
  if (_responseDataHook) {
    _responseDataHook(data, _manager);
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (_responseCloseHook) {
    _responseCloseHook(trailingMetadata, error, _manager);
  }
}

- (void)didWriteData {
  if (_didWriteDataHook) {
    _didWriteDataHook(_manager);
  }
}

@end

@interface GlobalInterceptorFactory : HookInterceptorFactory

@property BOOL enabled;

- (instancetype)initWithDispatchQueue:(dispatch_queue_t)dispatchQueue;

- (void)setStartHook:(void (^)(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                               GRPCInterceptorManager *manager))startHook
              writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                 finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
    receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                      GRPCInterceptorManager *manager))receiveNextMessagesHook
         responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                      GRPCInterceptorManager *manager))responseHeaderHook
           responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
          responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                      GRPCInterceptorManager *manager))responseCloseHook
           didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook;

@end

@implementation GlobalInterceptorFactory

- (instancetype)initWithDispatchQueue:(dispatch_queue_t)dispatchQueue {
  _enabled = NO;
  return [super initWithDispatchQueue:dispatchQueue
                            startHook:nil
                        writeDataHook:nil
                           finishHook:nil
              receiveNextMessagesHook:nil
                   responseHeaderHook:nil
                     responseDataHook:nil
                    responseCloseHook:nil
                     didWriteDataHook:nil];
}

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager {
  if (_enabled) {
    return [[HookInterceptor alloc] initWithInterceptorManager:interceptorManager
                                                 dispatchQueue:_dispatchQueue
                                                     startHook:_startHook
                                                 writeDataHook:_writeDataHook
                                                    finishHook:_finishHook
                                       receiveNextMessagesHook:_receiveNextMessagesHook
                                            responseHeaderHook:_responseHeaderHook
                                              responseDataHook:_responseDataHook
                                             responseCloseHook:_responseCloseHook
                                              didWriteDataHook:_didWriteDataHook];
  } else {
    return nil;
  }
}

- (void)setStartHook:(void (^)(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                               GRPCInterceptorManager *manager))startHook
              writeDataHook:(void (^)(id data, GRPCInterceptorManager *manager))writeDataHook
                 finishHook:(void (^)(GRPCInterceptorManager *manager))finishHook
    receiveNextMessagesHook:(void (^)(NSUInteger numberOfMessages,
                                      GRPCInterceptorManager *manager))receiveNextMessagesHook
         responseHeaderHook:(void (^)(NSDictionary *initialMetadata,
                                      GRPCInterceptorManager *manager))responseHeaderHook
           responseDataHook:(void (^)(id data, GRPCInterceptorManager *manager))responseDataHook
          responseCloseHook:(void (^)(NSDictionary *trailingMetadata, NSError *error,
                                      GRPCInterceptorManager *manager))responseCloseHook
           didWriteDataHook:(void (^)(GRPCInterceptorManager *manager))didWriteDataHook {
  _startHook = startHook;
  _writeDataHook = writeDataHook;
  _finishHook = finishHook;
  _receiveNextMessagesHook = receiveNextMessagesHook;
  _responseHeaderHook = responseHeaderHook;
  _responseDataHook = responseDataHook;
  _responseCloseHook = responseCloseHook;
  _didWriteDataHook = didWriteDataHook;
}

@end

static GlobalInterceptorFactory *globalInterceptorFactory = nil;
static dispatch_once_t initGlobalInterceptorFactory;

#pragma mark Tests

@implementation InteropTests

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
- (void)retriableTest:(SEL)selector retries:(int)retries timeout:(NSTimeInterval)timeout {
  for (int i = 0; i < retries; i++) {
    NSDate *waitUntil = [[NSDate date] dateByAddingTimeInterval:timeout];
    NSCondition *cv = [[NSCondition alloc] init];
    __block BOOL done = NO;
    [cv lock];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{
      [self performSelector:selector];
      [cv lock];
      done = YES;
      [cv signal];
      [cv unlock];
    });
    while (!done && [waitUntil timeIntervalSinceNow] > 0) {
      [cv waitUntilDate:waitUntil];
    }
    if (done) {
      [cv unlock];
      break;
    } else {
      [cv unlock];
      [self tearDown];
      [self setUp];
    }
  }
}
#pragma clang diagnostic pop

+ (XCTestSuite *)defaultTestSuite {
  if (self == [InteropTests class]) {
    return [XCTestSuite testSuiteWithName:@"InteropTestsEmptySuite"];
  }
  return super.defaultTestSuite;
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

// For backwards compatibility
+ (GRPCTransportType)transportType {
  return GRPCTransportTypeChttp2BoringSSL;
}

+ (GRPCTransportID)transport {
  return NULL;
}

+ (NSString *)PEMRootCertificates {
  return nil;
}

+ (NSString *)hostNameOverride {
  return nil;
}

+ (void)setUp {
  dispatch_once(&initGlobalInterceptorFactory, ^{
    dispatch_queue_t globalInterceptorQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    globalInterceptorFactory =
        [[GlobalInterceptorFactory alloc] initWithDispatchQueue:globalInterceptorQueue];
    [GRPCCall2 registerGlobalInterceptor:globalInterceptorFactory];
  });
}

- (void)setUp {
  self.continueAfterFailure = YES;
  [GRPCCall resetHostSettings];
  GRPCResetCallConnections();
  XCTAssertNotNil([[self class] host]);
}

- (void)testEmptyUnaryRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiter, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyUnary"];

    GPBEmpty *request = [GPBEmpty message];

    __weak RMTTestService *weakService = service;
    [service emptyCallWithRequest:request
                          handler:^(GPBEmpty *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }

                            XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                            id expectedResponse = [GPBEmpty message];
                            XCTAssertEqualObjects(response, expectedResponse);

                            [expectation fulfill];
                          }];
    waiter(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testEmptyUnaryRPCWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectReceive =
        [self expectationWithDescription:@"EmptyUnaryWithV2API received message"];
    __weak XCTestExpectation *expectComplete =
        [self expectationWithDescription:@"EmptyUnaryWithV2API completed"];

    GPBEmpty *request = [GPBEmpty message];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];

    __weak RMTTestService *weakService = service;
    GRPCUnaryProtoCall *call = [service
        emptyCallWithMessage:request
             responseHandler:[[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id message) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   if (message) {
                                     id expectedResponse = [GPBEmpty message];
                                     XCTAssertEqualObjects(message, expectedResponse);
                                     [expectReceive fulfill];
                                   }
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   XCTAssertNil(error, @"Unexpected error: %@", error);
                                   [expectComplete fulfill];
                                 }]
                 callOptions:options];
    [call start];
    waiterBlock(@[ expectReceive, expectComplete ], GRPCInteropTestTimeoutDefault);
  });
}

// Test that responses can be dispatched even if we do not run main run-loop
- (void)testAsyncDispatchWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];

    XCTestExpectation *receiveExpect = [self expectationWithDescription:@"receiveExpect"];
    XCTestExpectation *closeExpect = [self expectationWithDescription:@"closeExpect"];

    GPBEmpty *request = [GPBEmpty message];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];

    __block BOOL messageReceived = NO;
    __block BOOL done = NO;
    __weak RMTTestService *weakService = service;
    GRPCUnaryProtoCall *call = [service
        emptyCallWithMessage:request
             responseHandler:[[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id message) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   if (message) {
                                     id expectedResponse = [GPBEmpty message];
                                     XCTAssertEqualObjects(message, expectedResponse);
                                     messageReceived = YES;
                                   }
                                   [receiveExpect fulfill];
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   XCTAssertNil(error, @"Unexpected error: %@", error);
                                   done = YES;
                                   [closeExpect fulfill];
                                 }]
                 callOptions:options];

    [call start];

    waiterBlock(@[ receiveExpect, closeExpect ], GRPCInteropTestTimeoutDefault);
    XCTAssertTrue(messageReceived);
    XCTAssertTrue(done);
  });
}

- (void)testLargeUnaryRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = LARGE_RESPONSE_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:LARGE_REQUEST_PAYLOAD_SIZE];

    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }

                            XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                            RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                            expectedResponse.payload.type = RMTPayloadType_Compressable;
                            expectedResponse.payload.body =
                                [NSMutableData dataWithLength:LARGE_RESPONSE_PAYLOAD_SIZE];
                            XCTAssertEqualObjects(response, expectedResponse);

                            [expectation fulfill];
                          }];
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testUnaryResponseHandler {
  // The test does not work on a remote server since it does not echo a trailer
  if ([[self class] isRemoteTest]) {
    return;
  }

  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];

    XCTestExpectation *expectComplete = [self expectationWithDescription:@"call complete"];
    XCTestExpectation *expectCompleteMainQueue =
        [self expectationWithDescription:@"main queue call complete"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = LARGE_RESPONSE_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:LARGE_REQUEST_PAYLOAD_SIZE];

    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    const unsigned char raw_bytes[] = {1, 2, 3, 4};
    NSData *trailer_data = [NSData dataWithBytes:raw_bytes length:sizeof(raw_bytes)];
    options.initialMetadata = @{
      @"x-grpc-test-echo-trailing-bin" : trailer_data,
      @"x-grpc-test-echo-initial" : @"test-header"
    };

    __weak RMTTestService *weakService = service;

    __block GRPCUnaryResponseHandler *handler = [[GRPCUnaryResponseHandler alloc]
        initWithResponseHandler:^(GPBMessage *response, NSError *error) {
          if (weakService == nil) {
            return;
          }

          XCTAssertNil(error, @"Unexpected error: %@", error);
          RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
          expectedResponse.payload.type = RMTPayloadType_Compressable;
          expectedResponse.payload.body =
              [NSMutableData dataWithLength:LARGE_RESPONSE_PAYLOAD_SIZE];
          XCTAssertEqualObjects(response, expectedResponse);
          XCTAssertEqualObjects(handler.responseHeaders[@"x-grpc-test-echo-initial"],
                                @"test-header");
          XCTAssertEqualObjects(handler.responseTrailers[@"x-grpc-test-echo-trailing-bin"],
                                trailer_data);
          [expectComplete fulfill];
        }
          responseDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)];
    __block GRPCUnaryResponseHandler *handlerMainQueue = [[GRPCUnaryResponseHandler alloc]
        initWithResponseHandler:^(GPBMessage *response, NSError *error) {
          if (weakService == nil) {
            return;
          }
          XCTAssertNil(error, @"Unexpected error: %@", error);
          RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
          expectedResponse.payload.type = RMTPayloadType_Compressable;
          expectedResponse.payload.body =
              [NSMutableData dataWithLength:LARGE_RESPONSE_PAYLOAD_SIZE];
          XCTAssertEqualObjects(response, expectedResponse);
          XCTAssertEqualObjects(handlerMainQueue.responseHeaders[@"x-grpc-test-echo-initial"],
                                @"test-header");
          XCTAssertEqualObjects(handlerMainQueue.responseTrailers[@"x-grpc-test-echo-trailing-bin"],
                                trailer_data);
          [expectCompleteMainQueue fulfill];
        }
          responseDispatchQueue:nil];

    [[service unaryCallWithMessage:request responseHandler:handler callOptions:options] start];
    [[service unaryCallWithMessage:request responseHandler:handlerMainQueue
                       callOptions:options] start];

    waiterBlock(@[ expectComplete, expectCompleteMainQueue ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testLargeUnaryRPCWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectReceive =
        [self expectationWithDescription:@"LargeUnaryWithV2API received message"];
    __weak XCTestExpectation *expectComplete =
        [self expectationWithDescription:@"LargeUnaryWithV2API received complete"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = LARGE_RESPONSE_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:LARGE_REQUEST_PAYLOAD_SIZE];

    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];

    __weak RMTTestService *weakService = service;
    GRPCUnaryProtoCall *call = [service
        unaryCallWithMessage:request
             responseHandler:[[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id message) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   XCTAssertNotNil(message);
                                   if (message) {
                                     RMTSimpleResponse *expectedResponse =
                                         [RMTSimpleResponse message];
                                     expectedResponse.payload.type = RMTPayloadType_Compressable;
                                     expectedResponse.payload.body =
                                         [NSMutableData dataWithLength:LARGE_RESPONSE_PAYLOAD_SIZE];
                                     XCTAssertEqualObjects(message, expectedResponse);

                                     [expectReceive fulfill];
                                   }
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   if (weakService == nil) {
                                     return;
                                   }
                                   XCTAssertNil(error, @"Unexpected error: %@", error);
                                   [expectComplete fulfill];
                                 }]
                 callOptions:options];
    [call start];
    waiterBlock(@[ expectReceive, expectComplete ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testConcurrentRPCsWithErrorsWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    NSMutableArray *completeExpectations = [NSMutableArray array];
    NSMutableArray *calls = [NSMutableArray array];
    int num_rpcs = 10;
    for (int i = 0; i < num_rpcs; ++i) {
      [completeExpectations
          addObject:[self expectationWithDescription:
                              [NSString stringWithFormat:@"Received trailer for RPC %d", i]]];

      RMTSimpleRequest *request = [RMTSimpleRequest message];
      request.responseType = RMTPayloadType_Compressable;
      request.responseSize = SMALL_PAYLOAD_SIZE;
      request.payload.body = [NSMutableData dataWithLength:SMALL_PAYLOAD_SIZE];
      if (i % 3 == 0) {
        request.responseStatus.code = GRPC_STATUS_UNAVAILABLE;
      } else if (i % 7 == 0) {
        request.responseStatus.code = GRPC_STATUS_CANCELLED;
      }
      GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
      // For backwards compatibility
      options.transportType = [[self class] transportType];
      options.transport = [[self class] transport];
      options.PEMRootCertificates = [[self class] PEMRootCertificates];
      options.hostNameOverride = [[self class] hostNameOverride];

      __weak RMTTestService *weakService = service;
      GRPCUnaryProtoCall *call = [service
          unaryCallWithMessage:request
               responseHandler:[[InteropTestsBlockCallbacks alloc]
                                   initWithInitialMetadataCallback:nil
                                   messageCallback:^(id message) {
                                     if (weakService == nil) {
                                       return;
                                     }
                                     if (message) {
                                       RMTSimpleResponse *expectedResponse =
                                           [RMTSimpleResponse message];
                                       expectedResponse.payload.type = RMTPayloadType_Compressable;
                                       expectedResponse.payload.body =
                                           [NSMutableData dataWithLength:SMALL_PAYLOAD_SIZE];
                                       XCTAssertEqualObjects(message, expectedResponse);
                                     }
                                   }
                                   closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                     if (weakService == nil) {
                                       return;
                                     }
                                     [completeExpectations[i] fulfill];
                                   }]
                   callOptions:options];
      [calls addObject:call];
    }

    for (int i = 0; i < num_rpcs; ++i) {
      GRPCUnaryProtoCall *call = calls[i];
      [call start];
    }

    waiterBlock(completeExpectations, GRPCInteropTestTimeoutDefault);
  });
}

- (void)concurrentRPCsWithErrors {
  const int kNumRpcs = 10;
  __block int completedCallCount = 0;
  NSCondition *cv = [[NSCondition alloc] init];
  NSDate *waitUntil = [[NSDate date] dateByAddingTimeInterval:GRPCInteropTestTimeoutDefault];
  [cv lock];
  for (int i = 0; i < kNumRpcs; ++i) {
    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = SMALL_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:SMALL_PAYLOAD_SIZE];
    if (i % 3 == 0) {
      request.responseStatus.code = GRPC_STATUS_UNAVAILABLE;
    } else if (i % 7 == 0) {
      request.responseStatus.code = GRPC_STATUS_CANCELLED;
    }

    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak RMTTestService *weakService = service;
    GRPCProtoCall *call = [service
        RPCToUnaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }
                            if (error == nil) {
                              RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                              expectedResponse.payload.type = RMTPayloadType_Compressable;
                              expectedResponse.payload.body =
                                  [NSMutableData dataWithLength:SMALL_PAYLOAD_SIZE];
                              XCTAssertEqualObjects(response, expectedResponse);
                            }
                            // DEBUG
                            [cv lock];
                            if (++completedCallCount == kNumRpcs) {
                              [cv signal];
                            }
                            [cv unlock];
                          }];
    [call setResponseDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)];
    [call start];
  }
  while (completedCallCount < kNumRpcs && [waitUntil timeIntervalSinceNow] > 0) {
    [cv waitUntilDate:waitUntil];
  }
  [cv unlock];
}

- (void)testConcurrentRPCsWithErrors {
  [self retriableTest:@selector(concurrentRPCsWithErrors) retries:kTestRetries timeout:10];
}

- (void)testPacketCoalescing {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = SMALL_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:SMALL_PAYLOAD_SIZE];

    [GRPCCall enableOpBatchLog:YES];
    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }
                            XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                            RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                            expectedResponse.payload.type = RMTPayloadType_Compressable;
                            expectedResponse.payload.body = [NSMutableData dataWithLength:10];
                            XCTAssertEqualObjects(response, expectedResponse);

                            // The test is a success if there is a batch of exactly 3 ops
                            // (SEND_INITIAL_METADATA, SEND_MESSAGE, SEND_CLOSE_FROM_CLIENT).
                            // Without packet coalescing each batch of ops contains only one op.
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

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
    [GRPCCall enableOpBatchLog:NO];
  });
}

- (void)test4MBResponsesAreAccepted {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"MaxResponseSize"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    const int32_t kPayloadSize =
        4 * 1024 * 1024 - self.encodingOverhead;  // 4MB - encoding overhead
    request.responseSize = kPayloadSize;

    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }
                            XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                            XCTAssertEqual(response.payload.body.length, kPayloadSize);
                            [expectation fulfill];
                          }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testResponsesOverMaxSizeFailWithActionableMessage {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"ResponseOverMaxSize"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    const int32_t kPayloadSize = 4 * 1024 * 1024 - self.encodingOverhead + 1;  // 1B over max size
    request.responseSize = kPayloadSize;

    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }
                            // TODO(jcanizales): Catch the error and rethrow it with an
                            // actionable message:
                            // - Use +[GRPCCall setResponseSizeLimit:forHost:] to set a
                            // higher limit.
                            // - If you're developing the server, consider using response
                            // streaming, or let clients filter
                            //   responses by setting a google.protobuf.FieldMask in the
                            //   request:
                            //   https://github.com/protocolbuffers/protobuf/blob/master/src/google/protobuf/field_mask.proto
                            XCTAssertEqualObjects(
                                error.localizedDescription,
                                @"Received message larger than max (4194305 vs. 4194304)");
                            [expectation fulfill];
                          }];
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testResponsesOver4MBAreAcceptedIfOptedIn {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"HigherResponseSizeLimit"];
    __block NSError *callError = nil;

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    const size_t kPayloadSize = 5 * 1024 * 1024;  // 5MB
    request.responseSize = kPayloadSize;

    [GRPCCall setResponseSizeLimit:6 * 1024 * 1024 forHost:[[self class] host]];
    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }
                            callError = error;
                            [expectation fulfill];
                          }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
    XCTAssertNil(callError, @"Finished with unexpected error: %@", callError);
  });
}

- (void)testClientStreamingRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
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

    __weak RMTTestService *weakService = service;
    [service
        streamingInputCallWithRequestsWriter:writer
                                     handler:^(RMTStreamingInputCallResponse *response,
                                               NSError *error) {
                                       if (weakService == nil) {
                                         return;
                                       }
                                       XCTAssertNil(error, @"Finished with unexpected error: %@",
                                                    error);

                                       RMTStreamingInputCallResponse *expectedResponse =
                                           [RMTStreamingInputCallResponse message];
                                       expectedResponse.aggregatedPayloadSize = 74922;
                                       XCTAssertEqualObjects(response, expectedResponse);

                                       [expectation fulfill];
                                     }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testServerStreamingRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"ServerStreaming"];

    NSArray *expectedSizes = @[ @31415, @9, @2653, @58979 ];

    RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
    for (NSNumber *size in expectedSizes) {
      RMTResponseParameters *parameters = [RMTResponseParameters message];
      parameters.size = [size intValue];
      [request.responseParametersArray addObject:parameters];
    }

    __block int index = 0;
    __weak RMTTestService *weakService = service;
    [service
        streamingOutputCallWithRequest:request
                          eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                         NSError *error) {
                            if (weakService == nil) {
                              return;
                            }

                            assertBlock(
                                error == nil,
                                [NSString
                                    stringWithFormat:@"Finished with unexpected error: %@", error]);
                            assertBlock(done || response,
                                        @"Event handler called without an event.");

                            if (response) {
                              assertBlock(index < 4, @"More than 4 responses received.");

                              id expected = [RMTStreamingOutputCallResponse
                                  messageWithPayloadSize:expectedSizes[index]];
                              assertBlock(
                                  [response isEqual:expected],
                                  [NSString
                                      stringWithFormat:@"response %@ not equal to expected %@",
                                                       response, expected]);

                              index += 1;
                            }

                            if (done) {
                              assertBlock(
                                  index == 4,
                                  [NSString stringWithFormat:@"Received %@ responses instead of 4.",
                                                             @(index)]);
                              [expectation fulfill];
                            }
                          }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testPingPongRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPong"];

    NSArray *requests = @[ @27182, @8, @1828, @45904 ];
    NSArray *responses = @[ @31415, @9, @2653, @58979 ];

    GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    [requestsBuffer writeValue:request];

    __weak RMTTestService *weakService = service;
    [service
        fullDuplexCallWithRequestsWriter:requestsBuffer
                            eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                           NSError *error) {
                              if (weakService == nil) {
                                return;
                              }

                              assertBlock(
                                  error == nil,
                                  [NSString stringWithFormat:@"Finished with unexpected error: %@",
                                                             error]);
                              assertBlock(done || response,
                                          @"Event handler called without an event.");

                              if (response) {
                                assertBlock(index < 4, @"More than 4 responses received.");

                                id expected = [RMTStreamingOutputCallResponse
                                    messageWithPayloadSize:responses[index]];
                                assertBlock(
                                    [response isEqual:expected],
                                    [NSString
                                        stringWithFormat:@"response %@ not equal to expected %@",
                                                         response, expected]);

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
                                assertBlock(
                                    index == 4,
                                    [NSString
                                        stringWithFormat:@"Received %@ responses instead of 4.",
                                                         @(index)]);
                                [expectation fulfill];
                              }
                            }];
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testPingPongRPCWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPongWithV2API"];

    NSArray *requests = @[ @27182, @8, @1828, @45904 ];
    NSArray *responses = @[ @31415, @9, @2653, @58979 ];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];

    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }
                  assertBlock(index < 4, @"More than 4 responses received.");

                  id expected =
                      [RMTStreamingOutputCallResponse messageWithPayloadSize:responses[index]];
                  assertBlock([message isEqual:expected],
                              [NSString stringWithFormat:@"message %@ not equal to expected %@",
                                                         message, expected]);
                  index += 1;
                  if (index < 4) {
                    id request =
                        [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                        requestedResponseSize:responses[index]];
                    [localCall writeMessage:request];
                  } else {
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }
                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);
                  assertBlock(
                      index == 4,
                      [NSString stringWithFormat:@"Received %@ responses instead of 4.", @(index)]);
                  [expectation fulfill];
                }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call writeMessage:request];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testPingPongRPCWithFlowControl {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPongWithV2API"];

    NSArray *requests = @[ @27182, @8, @1828, @45904 ];
    NSArray *responses = @[ @31415, @9, @2653, @58979 ];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.flowControlEnabled = YES;
    __block int writeMessageCount = 0;

    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }

                  assertBlock((index < 4), @"More than 4 responses received.");
                  id expected =
                      [RMTStreamingOutputCallResponse messageWithPayloadSize:responses[index]];
                  assertBlock(
                      [message isEqual:expected],
                      [NSString stringWithFormat:@"message %@ not equal to %@", message, expected]);

                  index += 1;
                  if (index < 4) {
                    id request =
                        [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                        requestedResponseSize:responses[index]];
                    [localCall writeMessage:request];
                    [localCall receiveNextMessage];
                  } else {
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }

                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);
                  assertBlock(
                      index == 4,
                      [NSString stringWithFormat:@"Received %i responses instead of 4.", index]);
                  [expectation fulfill];
                }
                writeMessageCallback:^{
                  writeMessageCount++;
                }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call receiveNextMessage];
    [call writeMessage:request];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
    assertBlock(
        writeMessageCount == 4,
        [NSString stringWithFormat:@"writeMessageCount %@ not equal to 4", @(writeMessageCount)]);
  });
}

- (void)testEmptyStreamRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"EmptyStream"];
    __weak RMTTestService *weakService = service;
    [service
        fullDuplexCallWithRequestsWriter:[GRXWriter emptyWriter]
                            eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                           NSError *error) {
                              if (weakService == nil) {
                                return;
                              }
                              XCTAssertNil(error, @"Finished with unexpected error: %@", error);
                              XCTAssert(done, @"Unexpected response: %@", response);
                              [expectation fulfill];
                            }];
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCancelAfterBeginRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"CancelAfterBegin"];

    // A buffered pipe to which we never write any value acts as a writer that just hangs.
    GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

    __weak RMTTestService *weakService = service;
    GRPCProtoCall *call = [service
        RPCToStreamingInputCallWithRequestsWriter:requestsBuffer
                                          handler:^(RMTStreamingInputCallResponse *response,
                                                    NSError *error) {
                                            if (weakService == nil) {
                                              return;
                                            }
                                            XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
                                            [expectation fulfill];
                                          }];
    XCTAssertEqual(call.state, GRXWriterStateNotStarted);

    [call start];
    XCTAssertEqual(call.state, GRXWriterStateStarted);

    [call cancel];
    XCTAssertEqual(call.state, GRXWriterStateFinished);

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCancelAfterBeginRPCWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"CancelAfterBeginWithV2API"];

    // A buffered pipe to which we never write any value acts as a writer that just hangs.
    __weak RMTTestService *weakService = service;
    GRPCStreamingProtoCall *call = [service
        streamingInputCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                                  initWithInitialMetadataCallback:nil
                                                  messageCallback:^(id message) {
                                                    if (weakService == nil) {
                                                      return;
                                                    }
                                                    XCTFail(@"Not expected to receive message");
                                                  }
                                                  closeCallback:^(NSDictionary *trailingMetadata,
                                                                  NSError *error) {
                                                    if (weakService == nil) {
                                                      return;
                                                    }
                                                    XCTAssertEqual(error.code,
                                                                   GRPC_STATUS_CANCELLED);
                                                    [expectation fulfill];
                                                  }]
                                  callOptions:nil];
    [call start];
    [call cancel];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCancelAfterFirstResponseRPC {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"CancelAfterFirstResponse"];

    // A buffered pipe to which we write a single value but never close
    GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

    __block BOOL receivedResponse = NO;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@21782
                                                 requestedResponseSize:@31415];

    [requestsBuffer writeValue:request];

    __weak RMTTestService *weakService = service;
    __block GRPCProtoCall *call = [service
        RPCToFullDuplexCallWithRequestsWriter:requestsBuffer
                                 eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                                NSError *error) {
                                   if (weakService == nil) {
                                     return;
                                   }
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
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCancelAfterFirstResponseRPCWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *completionExpectation =
        [self expectationWithDescription:@"Call completed."];
    __weak XCTestExpectation *responseExpectation =
        [self expectationWithDescription:@"Received response."];

    __block BOOL receivedResponse = NO;

    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = self.class.transportType;
    options.transport = [[self class] transport];
    options.PEMRootCertificates = self.class.PEMRootCertificates;
    options.hostNameOverride = [[self class] hostNameOverride];

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@21782
                                                 requestedResponseSize:@31415];

    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                              initWithInitialMetadataCallback:nil
                                              messageCallback:^(id message) {
                                                GRPCStreamingProtoCall *localCall = weakCall;
                                                if (localCall == nil) {
                                                  return;
                                                }
                                                XCTAssertFalse(receivedResponse);
                                                receivedResponse = YES;
                                                [localCall cancel];
                                                [responseExpectation fulfill];
                                              }
                                              closeCallback:^(NSDictionary *trailingMetadata,
                                                              NSError *error) {
                                                if (weakCall == nil) {
                                                  return;
                                                }
                                                XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
                                                [completionExpectation fulfill];
                                              }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call writeMessage:request];
    waiterBlock(@[ completionExpectation, responseExpectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCancelAfterFirstRequestWithV2API {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *completionExpectation =
        [self expectationWithDescription:@"Call completed."];

    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = self.class.transportType;
    options.transport = [[self class] transport];
    options.PEMRootCertificates = self.class.PEMRootCertificates;
    options.hostNameOverride = [[self class] hostNameOverride];

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@21782
                                                 requestedResponseSize:@31415];

    __weak RMTTestService *weakService = service;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                              initWithInitialMetadataCallback:nil
                                              messageCallback:^(id message) {
                                                if (weakService == nil) {
                                                  return;
                                                }
                                                XCTFail(@"Received unexpected response.");
                                              }
                                              closeCallback:^(NSDictionary *trailingMetadata,
                                                              NSError *error) {
                                                if (weakService == nil) {
                                                  return;
                                                }
                                                XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
                                                [completionExpectation fulfill];
                                              }]
                              callOptions:options];
    [call start];
    [call writeMessage:request];
    [call cancel];
    waiterBlock(@[ completionExpectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testRPCAfterClosingOpenConnections {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"RPC after closing connection"];

    GPBEmpty *request = [GPBEmpty message];

    __weak RMTTestService *weakService = service;
    [service
        emptyCallWithRequest:request
                     handler:^(GPBEmpty *response, NSError *error) {
                       if (weakService == nil) {
                         return;
                       }
                       XCTAssertNil(error, @"First RPC finished with unexpected error: %@", error);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                       [GRPCCall closeOpenConnections];
#pragma clang diagnostic pop

                       [weakService
                           emptyCallWithRequest:request
                                        handler:^(GPBEmpty *response, NSError *error) {
                                          XCTAssertNil(
                                              error,
                                              @"Second RPC finished with unexpected error: %@",
                                              error);
                                          [expectation fulfill];
                                        }];
                     }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testCompressedUnaryRPC {
  // This test needs to be disabled for remote test because interop server grpc-test
  // does not support compression.
  if (isRemoteInteropTest([[self class] host])) {
    return;
  }

  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"LargeUnary"];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = LARGE_RESPONSE_PAYLOAD_SIZE;
    request.payload.body = [NSMutableData dataWithLength:LARGE_REQUEST_PAYLOAD_SIZE];
    request.expectCompressed.value = YES;
    [GRPCCall setDefaultCompressMethod:GRPCCompressGzip forhost:[[self class] host]];

    __weak RMTTestService *weakService = service;
    [service unaryCallWithRequest:request
                          handler:^(RMTSimpleResponse *response, NSError *error) {
                            if (weakService == nil) {
                              return;
                            }

                            XCTAssertNil(error, @"Finished with unexpected error: %@", error);

                            RMTSimpleResponse *expectedResponse = [RMTSimpleResponse message];
                            expectedResponse.payload.type = RMTPayloadType_Compressable;
                            expectedResponse.payload.body =
                                [NSMutableData dataWithLength:LARGE_RESPONSE_PAYLOAD_SIZE];
                            XCTAssertEqualObjects(response, expectedResponse);

                            [expectation fulfill];
                          }];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

// TODO(b/268379869): This test has a race and is flaky in any configurations. One possible way to
// deflake this test is to find a way to disable ping ack on the interop server for this test case.
- (void)testKeepaliveWithV2API {
  return;

  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    if ([[self class] transport] == gGRPCCoreCronetID) {
      // Cronet does not support keepalive
      return;
    }
    __weak XCTestExpectation *expectation = [self expectationWithDescription:@"Keepalive"];

    const NSTimeInterval kTestTimeout = 5;
    NSNumber *kRequestSize = @27182;
    NSNumber *kResponseSize = @31415;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:kRequestSize
                                                 requestedResponseSize:kResponseSize];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.keepaliveInterval = 1.5;
    options.keepaliveTimeout = 0;

    __weak RMTTestService *weakService = service;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc]
                initWithInitialMetadataCallback:nil
                                messageCallback:nil
                                  closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                    if (weakService == nil) {
                                      return;
                                    }
                                    XCTAssertNotNil(error);
                                    XCTAssertEqual(
                                        error.code, GRPC_STATUS_UNAVAILABLE,
                                        @"Received status %@ instead of UNAVAILABLE (14).",
                                        @(error.code));
                                    [expectation fulfill];
                                  }]
                              callOptions:options];
    [call writeMessage:request];
    [call start];

    waiterBlock(@[ expectation ], kTestTimeout);
    [call finish];
  });
}

- (void)testDefaultInterceptor {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"testDefaultInterceptor"];

    NSArray *requests = @[ @27182, @8, @1828, @45904 ];
    NSArray *responses = @[ @31415, @9, @2653, @58979 ];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.interceptorFactories = @[ [[DefaultInterceptorFactory alloc] init] ];

    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }
                  assertBlock(index < 4, @"More than 4 responses received.");

                  id expected =
                      [RMTStreamingOutputCallResponse messageWithPayloadSize:responses[index]];
                  assertBlock([message isEqual:expected],
                              [NSString stringWithFormat:@"message %@ not equal to expected %@",
                                                         message, expected]);

                  index += 1;
                  if (index < 4) {
                    id request =
                        [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                        requestedResponseSize:responses[index]];
                    [localCall writeMessage:request];
                  } else {
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }

                  assertBlock(
                      index == 4,
                      [NSString stringWithFormat:@"Received %@ responses instead of 4.", @(index)]);

                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);

                  [expectation fulfill];
                }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call writeMessage:request];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
  });
}

- (void)testLoggingInterceptor {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"testLoggingInterceptor"];

    __block NSUInteger startCount = 0;
    __block NSUInteger writeDataCount = 0;
    __block NSUInteger finishCount = 0;
    __block NSUInteger receiveNextMessageCount = 0;
    __block NSUInteger responseHeaderCount = 0;
    __block NSUInteger responseDataCount = 0;
    __block NSUInteger responseCloseCount = 0;
    __block NSUInteger didWriteDataCount = 0;
    id<GRPCInterceptorFactory> factory = [[HookInterceptorFactory alloc]
        initWithDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)
        startHook:^(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                    GRPCInterceptorManager *manager) {
          startCount++;
          XCTAssertEqualObjects(requestOptions.host, [[self class] host]);
          XCTAssertEqualObjects(requestOptions.path, @"/grpc.testing.TestService/FullDuplexCall");
          XCTAssertEqual(requestOptions.safety, GRPCCallSafetyDefault);
          [manager startNextInterceptorWithRequest:[requestOptions copy]
                                       callOptions:[callOptions copy]];
        }
        writeDataHook:^(id data, GRPCInterceptorManager *manager) {
          writeDataCount++;
          [manager writeNextInterceptorWithData:data];
        }
        finishHook:^(GRPCInterceptorManager *manager) {
          finishCount++;
          [manager finishNextInterceptor];
        }
        receiveNextMessagesHook:^(NSUInteger numberOfMessages, GRPCInterceptorManager *manager) {
          receiveNextMessageCount++;
          [manager receiveNextInterceptorMessages:numberOfMessages];
        }
        responseHeaderHook:^(NSDictionary *initialMetadata, GRPCInterceptorManager *manager) {
          responseHeaderCount++;
          [manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
        }
        responseDataHook:^(id data, GRPCInterceptorManager *manager) {
          responseDataCount++;
          [manager forwardPreviousInterceptorWithData:data];
        }
        responseCloseHook:^(NSDictionary *trailingMetadata, NSError *error,
                            GRPCInterceptorManager *manager) {
          responseCloseCount++;
          [manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata
                                                                 error:error];
        }
        didWriteDataHook:^(GRPCInterceptorManager *manager) {
          didWriteDataCount++;
          [manager forwardPreviousInterceptorDidWriteData];
        }];

    NSArray *requests = @[ @1, @2, @3, @4 ];
    NSArray *responses = @[ @1, @2, @3, @4 ];

    __block int messageIndex = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[messageIndex]
                                                 requestedResponseSize:responses[messageIndex]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.flowControlEnabled = YES;
    options.interceptorFactories = @[ factory ];

    __block int writeMessageCount = 0;
    __block __weak GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }
                  assertBlock((messageIndex < 4), @"More than 4 responses received.");

                  id expected = [RMTStreamingOutputCallResponse
                      messageWithPayloadSize:responses[messageIndex]];
                  assertBlock([message isEqual:expected],
                              [NSString stringWithFormat:@"message %@ not equal to expected %@",
                                                         message, expected]);
                  messageIndex += 1;
                  if (messageIndex < 4) {
                    id request = [RMTStreamingOutputCallRequest
                        messageWithPayloadSize:requests[messageIndex]
                         requestedResponseSize:responses[messageIndex]];
                    [localCall writeMessage:request];
                    [localCall receiveNextMessage];
                  } else {
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }
                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);
                  assertBlock(messageIndex == 4,
                              [NSString stringWithFormat:@"Received %@ responses instead of 4.",
                                                         @(messageIndex)]);
                  [expectation fulfill];
                }
                writeMessageCallback:^{
                  writeMessageCount++;
                }]
                              callOptions:options];

    weakCall = call;
    [call start];
    [call receiveNextMessage];
    [call writeMessage:request];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);

    assertBlock(startCount == 1, [NSString stringWithFormat:@"%@", @(startCount)]);
    assertBlock(writeDataCount == 4, [NSString stringWithFormat:@"%@", @(writeDataCount)]);
    assertBlock(finishCount == 1, [NSString stringWithFormat:@"%@", @(finishCount)]);
    assertBlock(receiveNextMessageCount == 4,
                [NSString stringWithFormat:@"%@", @(receiveNextMessageCount)]);
    assertBlock(responseHeaderCount == 1,
                [NSString stringWithFormat:@"%@", @(responseHeaderCount)]);
    assertBlock(responseDataCount == 4, [NSString stringWithFormat:@"%@", @(responseDataCount)]);
    assertBlock(responseCloseCount == 1, [NSString stringWithFormat:@"%@", @(responseCloseCount)]);
    assertBlock(didWriteDataCount == 4, [NSString stringWithFormat:@"%@", @(didWriteDataCount)]);
    assertBlock(writeMessageCount == 4, [NSString stringWithFormat:@"%@", @(writeMessageCount)]);
  });
}

// Chain a default interceptor and a hook interceptor which, after one write, cancels the call
// under the hood but forward further data to the user.
- (void)testHijackingInterceptor {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    NSUInteger kCancelAfterWrites = 1;
    __weak XCTestExpectation *expectUserCallComplete =
        [self expectationWithDescription:@"User call completed."];
    __weak XCTestExpectation *expectResponseCallbackComplete =
        [self expectationWithDescription:@"Hook interceptor response callback completed"];

    NSArray *responses = @[ @1, @2, @3, @4 ];
    __block int index = 0;

    __block NSUInteger startCount = 0;
    __block NSUInteger writeDataCount = 0;
    __block NSUInteger finishCount = 0;
    __block NSUInteger responseHeaderCount = 0;
    __block NSUInteger responseDataCount = 0;
    __block NSUInteger responseCloseCount = 0;
    id<GRPCInterceptorFactory> factory = [[HookInterceptorFactory alloc]
        initWithDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)
        startHook:^(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                    GRPCInterceptorManager *manager) {
          startCount++;
          [manager startNextInterceptorWithRequest:[requestOptions copy]
                                       callOptions:[callOptions copy]];
        }
        writeDataHook:^(id data, GRPCInterceptorManager *manager) {
          writeDataCount++;
          if (index < kCancelAfterWrites) {
            [manager writeNextInterceptorWithData:data];
          } else if (index == kCancelAfterWrites) {
            [manager cancelNextInterceptor];
            [manager forwardPreviousInterceptorWithData:[[RMTStreamingOutputCallResponse
                                                            messageWithPayloadSize:responses[index]]
                                                            data]];
          } else {  // (index > kCancelAfterWrites)
            [manager forwardPreviousInterceptorWithData:[[RMTStreamingOutputCallResponse
                                                            messageWithPayloadSize:responses[index]]
                                                            data]];
          }
        }
        finishHook:^(GRPCInterceptorManager *manager) {
          finishCount++;
          // finish must happen after the hijacking, so directly reply with a close
          [manager forwardPreviousInterceptorCloseWithTrailingMetadata:@{@"grpc-status" : @"0"}
                                                                 error:nil];
          [manager shutDown];
        }
        receiveNextMessagesHook:nil
        responseHeaderHook:^(NSDictionary *initialMetadata, GRPCInterceptorManager *manager) {
          responseHeaderCount++;
          [manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
        }
        responseDataHook:^(id data, GRPCInterceptorManager *manager) {
          responseDataCount++;
          [manager forwardPreviousInterceptorWithData:data];
        }
        responseCloseHook:^(NSDictionary *trailingMetadata, NSError *error,
                            GRPCInterceptorManager *manager) {
          responseCloseCount++;
          // since we canceled the call, it should return cancel error
          XCTAssertNil(trailingMetadata);
          XCTAssertNotNil(error);
          XCTAssertEqual(error.code, GRPC_STATUS_CANCELLED);
          [expectResponseCallbackComplete fulfill];
        }
        didWriteDataHook:nil];

    NSArray *requests = @[ @1, @2, @3, @4 ];

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.interceptorFactories = @[ [[DefaultInterceptorFactory alloc] init], factory ];

    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }

                  assertBlock(index < 4, @"More than 4 responses received.");

                  id expected =
                      [RMTStreamingOutputCallResponse messageWithPayloadSize:responses[index]];
                  assertBlock([message isEqual:expected],
                              [NSString stringWithFormat:@"message %@ not equal to expected %@",
                                                         message, expected]);
                  index += 1;
                  if (index < 4) {
                    id request =
                        [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                        requestedResponseSize:responses[index]];
                    [localCall writeMessage:request];
                    [localCall receiveNextMessage];
                  } else {
                    [self waitForExpectations:@[ expectResponseCallbackComplete ]
                                      timeout:GRPCInteropTestTimeoutDefault];
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }
                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);
                  assertBlock(
                      index == 4,
                      [NSString stringWithFormat:@"Received %@ responses instead of 4.", @(index)]);

                  [expectUserCallComplete fulfill];
                }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call receiveNextMessage];
    [call writeMessage:request];

    waiterBlock(@[ expectUserCallComplete ], GRPCInteropTestTimeoutDefault);
    assertBlock(startCount == 1, [NSString stringWithFormat:@"%@", @(startCount)]);
    assertBlock(writeDataCount == 4, [NSString stringWithFormat:@"%@", @(writeDataCount)]);
    assertBlock(finishCount == 1, [NSString stringWithFormat:@"%@", @(finishCount)]);
    assertBlock(responseHeaderCount == 1,
                [NSString stringWithFormat:@"%@", @(responseHeaderCount)]);
    assertBlock(responseDataCount == 1, [NSString stringWithFormat:@"%@", @(responseDataCount)]);
    assertBlock(responseCloseCount == 1, [NSString stringWithFormat:@"%@", @(responseCloseCount)]);
  });
}

- (void)testGlobalInterceptor {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"testGlobalInterceptor"];

    __block NSUInteger startCount = 0;
    __block NSUInteger writeDataCount = 0;
    __block NSUInteger finishCount = 0;
    __block NSUInteger receiveNextMessageCount = 0;
    __block NSUInteger responseHeaderCount = 0;
    __block NSUInteger responseDataCount = 0;
    __block NSUInteger responseCloseCount = 0;
    __block NSUInteger didWriteDataCount = 0;
    [globalInterceptorFactory
        setStartHook:^(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                       GRPCInterceptorManager *manager) {
          startCount++;
          XCTAssertEqualObjects(requestOptions.host, [[self class] host]);
          XCTAssertEqualObjects(requestOptions.path, @"/grpc.testing.TestService/FullDuplexCall");
          XCTAssertEqual(requestOptions.safety, GRPCCallSafetyDefault);
          [manager startNextInterceptorWithRequest:[requestOptions copy]
                                       callOptions:[callOptions copy]];
        }
        writeDataHook:^(id data, GRPCInterceptorManager *manager) {
          writeDataCount++;
          [manager writeNextInterceptorWithData:data];
        }
        finishHook:^(GRPCInterceptorManager *manager) {
          finishCount++;
          [manager finishNextInterceptor];
        }
        receiveNextMessagesHook:^(NSUInteger numberOfMessages, GRPCInterceptorManager *manager) {
          receiveNextMessageCount++;
          [manager receiveNextInterceptorMessages:numberOfMessages];
        }
        responseHeaderHook:^(NSDictionary *initialMetadata, GRPCInterceptorManager *manager) {
          responseHeaderCount++;
          [manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
        }
        responseDataHook:^(id data, GRPCInterceptorManager *manager) {
          responseDataCount++;
          [manager forwardPreviousInterceptorWithData:data];
        }
        responseCloseHook:^(NSDictionary *trailingMetadata, NSError *error,
                            GRPCInterceptorManager *manager) {
          responseCloseCount++;
          [manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata
                                                                 error:error];
        }
        didWriteDataHook:^(GRPCInterceptorManager *manager) {
          didWriteDataCount++;
          [manager forwardPreviousInterceptorDidWriteData];
        }];

    NSArray *requests = @[ @1, @2, @3, @4 ];
    NSArray *responses = @[ @1, @2, @3, @4 ];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.flowControlEnabled = YES;
    globalInterceptorFactory.enabled = YES;

    __block int writeMessageCount = 0;
    __weak __block GRPCStreamingProtoCall *weakCall;
    __block GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:
            [[InteropTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                messageCallback:^(id message) {
                  GRPCStreamingProtoCall *localCall = weakCall;
                  if (localCall == nil) {
                    return;
                  }
                  assertBlock(index < 4, @"More than 4 responses received.");

                  index += 1;
                  if (index < 4) {
                    id request =
                        [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                        requestedResponseSize:responses[index]];
                    [localCall writeMessage:request];
                    [localCall receiveNextMessage];
                  } else {
                    [localCall finish];
                  }
                }
                closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                  if (weakCall == nil) {
                    return;
                  }
                  assertBlock(
                      error == nil,
                      [NSString stringWithFormat:@"Finished with unexpected error: %@", error]);
                  [expectation fulfill];
                }
                writeMessageCallback:^{
                  writeMessageCount++;
                }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call receiveNextMessage];
    [call writeMessage:request];
    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);

    assertBlock(startCount == 1, [NSString stringWithFormat:@"%@", @(startCount)]);
    assertBlock(writeDataCount == 4, [NSString stringWithFormat:@"%@", @(writeDataCount)]);
    assertBlock(finishCount == 1, [NSString stringWithFormat:@"%@", @(finishCount)]);
    assertBlock(receiveNextMessageCount == 4,
                [NSString stringWithFormat:@"%@", @(receiveNextMessageCount)]);
    assertBlock(responseHeaderCount == 1,
                [NSString stringWithFormat:@"%@", @(responseHeaderCount)]);
    assertBlock(responseDataCount == 4, [NSString stringWithFormat:@"%@", @(responseDataCount)]);
    assertBlock(responseCloseCount == 1, [NSString stringWithFormat:@"%@", @(responseCloseCount)]);
    assertBlock(didWriteDataCount == 4, [NSString stringWithFormat:@"%@", @(didWriteDataCount)]);
    assertBlock(writeMessageCount == 4, [NSString stringWithFormat:@"%@", @(writeMessageCount)]);
    globalInterceptorFactory.enabled = NO;
  });
}

- (void)testConflictingGlobalInterceptors {
  id<GRPCInterceptorFactory> factory = [[HookInterceptorFactory alloc]
        initWithDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)
                    startHook:nil
                writeDataHook:nil
                   finishHook:nil
      receiveNextMessagesHook:nil
           responseHeaderHook:nil
             responseDataHook:nil
            responseCloseHook:nil
             didWriteDataHook:nil];
  @try {
    [GRPCCall2 registerGlobalInterceptor:factory];
    XCTFail(@"Did not receive an exception when registering global interceptor the second time");
  } @catch (NSException *exception) {
    // Do nothing; test passes
  }
}

- (void)testInterceptorAndGlobalInterceptor {
  GRPCTestRunWithFlakeRepeats(self, ^(GRPCTestWaiter waiterBlock, GRPCTestAssert assertBlock) {
    RMTTestService *service = [RMTTestService serviceWithHost:[[self class] host]];
    __weak XCTestExpectation *expectation =
        [self expectationWithDescription:@"testInterceptorAndGlobalInterceptor"];

    __block NSUInteger startCount = 0;
    __block NSUInteger writeDataCount = 0;
    __block NSUInteger finishCount = 0;
    __block NSUInteger receiveNextMessageCount = 0;
    __block NSUInteger responseHeaderCount = 0;
    __block NSUInteger responseDataCount = 0;
    __block NSUInteger responseCloseCount = 0;
    __block NSUInteger didWriteDataCount = 0;

    id<GRPCInterceptorFactory> factory = [[HookInterceptorFactory alloc]
        initWithDispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)
        startHook:^(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                    GRPCInterceptorManager *manager) {
          startCount++;
          XCTAssertEqualObjects(requestOptions.host, [[self class] host]);
          XCTAssertEqualObjects(requestOptions.path, @"/grpc.testing.TestService/FullDuplexCall");
          XCTAssertEqual(requestOptions.safety, GRPCCallSafetyDefault);
          [manager startNextInterceptorWithRequest:[requestOptions copy]
                                       callOptions:[callOptions copy]];
        }
        writeDataHook:^(id data, GRPCInterceptorManager *manager) {
          writeDataCount++;
          [manager writeNextInterceptorWithData:data];
        }
        finishHook:^(GRPCInterceptorManager *manager) {
          finishCount++;
          [manager finishNextInterceptor];
        }
        receiveNextMessagesHook:^(NSUInteger numberOfMessages, GRPCInterceptorManager *manager) {
          receiveNextMessageCount++;
          [manager receiveNextInterceptorMessages:numberOfMessages];
        }
        responseHeaderHook:^(NSDictionary *initialMetadata, GRPCInterceptorManager *manager) {
          responseHeaderCount++;
          [manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
        }
        responseDataHook:^(id data, GRPCInterceptorManager *manager) {
          responseDataCount++;
          [manager forwardPreviousInterceptorWithData:data];
        }
        responseCloseHook:^(NSDictionary *trailingMetadata, NSError *error,
                            GRPCInterceptorManager *manager) {
          responseCloseCount++;
          [manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata
                                                                 error:error];
        }
        didWriteDataHook:^(GRPCInterceptorManager *manager) {
          didWriteDataCount++;
          [manager forwardPreviousInterceptorDidWriteData];
        }];

    __block NSUInteger globalStartCount = 0;
    __block NSUInteger globalWriteDataCount = 0;
    __block NSUInteger globalFinishCount = 0;
    __block NSUInteger globalReceiveNextMessageCount = 0;
    __block NSUInteger globalResponseHeaderCount = 0;
    __block NSUInteger globalResponseDataCount = 0;
    __block NSUInteger globalResponseCloseCount = 0;
    __block NSUInteger globalDidWriteDataCount = 0;

    [globalInterceptorFactory
        setStartHook:^(GRPCRequestOptions *requestOptions, GRPCCallOptions *callOptions,
                       GRPCInterceptorManager *manager) {
          globalStartCount++;
          XCTAssertEqualObjects(requestOptions.host, [[self class] host]);
          XCTAssertEqualObjects(requestOptions.path, @"/grpc.testing.TestService/FullDuplexCall");
          XCTAssertEqual(requestOptions.safety, GRPCCallSafetyDefault);
          [manager startNextInterceptorWithRequest:[requestOptions copy]
                                       callOptions:[callOptions copy]];
        }
        writeDataHook:^(id data, GRPCInterceptorManager *manager) {
          globalWriteDataCount++;
          [manager writeNextInterceptorWithData:data];
        }
        finishHook:^(GRPCInterceptorManager *manager) {
          globalFinishCount++;
          [manager finishNextInterceptor];
        }
        receiveNextMessagesHook:^(NSUInteger numberOfMessages, GRPCInterceptorManager *manager) {
          globalReceiveNextMessageCount++;
          [manager receiveNextInterceptorMessages:numberOfMessages];
        }
        responseHeaderHook:^(NSDictionary *initialMetadata, GRPCInterceptorManager *manager) {
          globalResponseHeaderCount++;
          [manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
        }
        responseDataHook:^(id data, GRPCInterceptorManager *manager) {
          globalResponseDataCount++;
          [manager forwardPreviousInterceptorWithData:data];
        }
        responseCloseHook:^(NSDictionary *trailingMetadata, NSError *error,
                            GRPCInterceptorManager *manager) {
          globalResponseCloseCount++;
          [manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata
                                                                 error:error];
        }
        didWriteDataHook:^(GRPCInterceptorManager *manager) {
          globalDidWriteDataCount++;
          [manager forwardPreviousInterceptorDidWriteData];
        }];

    NSArray *requests = @[ @1, @2, @3, @4 ];
    NSArray *responses = @[ @1, @2, @3, @4 ];

    __block int index = 0;

    id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:requests[index]
                                                 requestedResponseSize:responses[index]];
    GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
    // For backwards compatibility
    options.transportType = [[self class] transportType];
    options.transport = [[self class] transport];
    options.PEMRootCertificates = [[self class] PEMRootCertificates];
    options.hostNameOverride = [[self class] hostNameOverride];
    options.flowControlEnabled = YES;
    options.interceptorFactories = @[ factory ];
    globalInterceptorFactory.enabled = YES;

    __block int writeMessageCount = 0;
    __weak __block GRPCStreamingProtoCall *weakCall;
    GRPCStreamingProtoCall *call = [service
        fullDuplexCallWithResponseHandler:[[InteropTestsBlockCallbacks alloc]
                                              initWithInitialMetadataCallback:nil
                                              messageCallback:^(id message) {
                                                GRPCStreamingProtoCall *localCall = weakCall;
                                                if (localCall == nil) {
                                                  return;
                                                }
                                                index += 1;
                                                if (index < 4) {
                                                  id request = [RMTStreamingOutputCallRequest
                                                      messageWithPayloadSize:requests[index]
                                                       requestedResponseSize:responses[index]];
                                                  [localCall writeMessage:request];
                                                  [localCall receiveNextMessage];
                                                } else {
                                                  [localCall finish];
                                                }
                                              }
                                              closeCallback:^(NSDictionary *trailingMetadata,
                                                              NSError *error) {
                                                if (weakCall == nil) {
                                                  return;
                                                }
                                                [expectation fulfill];
                                              }
                                              writeMessageCallback:^{
                                                writeMessageCount++;
                                              }]
                              callOptions:options];
    weakCall = call;
    [call start];
    [call receiveNextMessage];
    [call writeMessage:request];

    waiterBlock(@[ expectation ], GRPCInteropTestTimeoutDefault);
    assertBlock(startCount == 1, [NSString stringWithFormat:@"%@", @(startCount)]);
    assertBlock(writeDataCount == 4, [NSString stringWithFormat:@"%@", @(writeDataCount)]);
    assertBlock(finishCount == 1, [NSString stringWithFormat:@"%@", @(finishCount)]);
    assertBlock(receiveNextMessageCount == 4,
                [NSString stringWithFormat:@"%@", @(receiveNextMessageCount)]);
    assertBlock(responseHeaderCount == 1,
                [NSString stringWithFormat:@"%@", @(responseHeaderCount)]);
    assertBlock(responseDataCount == 4, [NSString stringWithFormat:@"%@", @(responseDataCount)]);
    assertBlock(responseCloseCount == 1, [NSString stringWithFormat:@"%@", @(responseCloseCount)]);
    assertBlock(didWriteDataCount == 4, [NSString stringWithFormat:@"%@", @(didWriteDataCount)]);
    assertBlock(globalStartCount == 1, [NSString stringWithFormat:@"%@", @(globalStartCount)]);
    assertBlock(globalWriteDataCount == 4,
                [NSString stringWithFormat:@"%@", @(globalWriteDataCount)]);
    assertBlock(globalFinishCount == 1, [NSString stringWithFormat:@"%@", @(globalFinishCount)]);
    assertBlock(globalReceiveNextMessageCount == 4,
                [NSString stringWithFormat:@"%@", @(globalReceiveNextMessageCount)]);
    assertBlock(globalResponseHeaderCount == 1,
                [NSString stringWithFormat:@"%@", @(globalResponseHeaderCount)]);
    assertBlock(globalResponseDataCount == 4,
                [NSString stringWithFormat:@"%@", @(globalResponseDataCount)]);
    assertBlock(globalResponseCloseCount == 1,
                [NSString stringWithFormat:@"%@", @(globalResponseCloseCount)]);
    assertBlock(globalDidWriteDataCount == 4,
                [NSString stringWithFormat:@"%@", @(globalDidWriteDataCount)]);
    assertBlock(writeMessageCount == 4, [NSString stringWithFormat:@"%@", @(writeMessageCount)]);
    globalInterceptorFactory.enabled = NO;
  });
}

@end
