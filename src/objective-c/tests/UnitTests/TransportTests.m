/*
 *
 * Copyright 2019 gRPC authors.
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
#import <GRPCClient/GRPCInterceptor.h>
#import <GRPCClient/GRPCTransport.h>
#import <XCTest/XCTest.h>

#define TEST_TIMEOUT (8.0)

static NSString *const kRemoteHost = @"grpc-test.sandbox.googleapis.com:443";

static const GRPCTransportID kFakeTransportID = "io.grpc.transport.unittest.fake";

@class GRPCFakeTransportFactory;
dispatch_once_t initFakeTransportFactory;
static GRPCFakeTransportFactory *fakeTransportFactory;

@interface GRPCFakeTransportFactory : NSObject <GRPCTransportFactory>

@property(atomic) GRPCTransport *nextTransportInstance;
- (void)setTransportInterceptorFactories:(NSArray<id<GRPCInterceptorFactory>> *)factories;

@end

@implementation GRPCFakeTransportFactory {
  NSArray<id<GRPCInterceptorFactory>> *_interceptorFactories;
}

+ (instancetype)sharedInstance {
  dispatch_once(&initFakeTransportFactory, ^{
    fakeTransportFactory = [[GRPCFakeTransportFactory alloc] init];
  });
  return fakeTransportFactory;
}

+ (void)load {
  [[GRPCTransportRegistry sharedInstance] registerTransportWithID:kFakeTransportID
                                                          factory:[self sharedInstance]];
}

- (GRPCTransport *)createTransportWithManager:(GRPCTransportManager *)transportManager {
  return _nextTransportInstance;
}

- (void)setTransportInterceptorFactories:(NSArray<id<GRPCInterceptorFactory>> *)factories {
  _interceptorFactories = [NSArray arrayWithArray:factories];
}

- (NSArray<id<GRPCInterceptorFactory>> *)transportInterceptorFactories {
  return _interceptorFactories;
}

@end

@interface PhonyInterceptor : GRPCInterceptor

@property(atomic) BOOL hit;

@end

@implementation PhonyInterceptor {
  GRPCInterceptorManager *_manager;
  BOOL _passthrough;
}

- (instancetype)initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                             dispatchQueue:(dispatch_queue_t)dispatchQueue
                               passthrough:(BOOL)passthrough {
  if (dispatchQueue == nil) {
    dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
  }
  if ((self = [super initWithInterceptorManager:interceptorManager dispatchQueue:dispatchQueue])) {
    _manager = interceptorManager;
    _passthrough = passthrough;
  }
  return self;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  self.hit = YES;
  if (_passthrough) {
    [super startWithRequestOptions:requestOptions callOptions:callOptions];
  } else {
    [_manager
        forwardPreviousInterceptorCloseWithTrailingMetadata:nil
                                                      error:
                                                          [NSError
                                                              errorWithDomain:kGRPCErrorDomain
                                                                         code:GRPCErrorCodeCancelled
                                                                     userInfo:@{
                                                                       NSLocalizedDescriptionKey :
                                                                           @"Canceled."
                                                                     }]];
    [_manager shutDown];
  }
}

@end

@interface PhonyInterceptorFactory : NSObject <GRPCInterceptorFactory>

- (instancetype)initWithPassthrough:(BOOL)passthrough;

@property(nonatomic, readonly) PhonyInterceptor *lastInterceptor;

@end

@implementation PhonyInterceptorFactory {
  BOOL _passthrough;
}

- (instancetype)initWithPassthrough:(BOOL)passthrough {
  if ((self = [super init])) {
    _passthrough = passthrough;
  }
  return self;
}

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager {
  _lastInterceptor = [[PhonyInterceptor alloc]
      initWithInterceptorManager:interceptorManager
                   dispatchQueue:dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL)
                     passthrough:_passthrough];
  return _lastInterceptor;
}

@end

@interface TestsBlockCallbacks : NSObject <GRPCResponseHandler>

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                   dataCallback:(void (^)(id))dataCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback
                           writeMessageCallback:(void (^)(void))writeMessageCallback;

@end

@implementation TestsBlockCallbacks {
  void (^_initialMetadataCallback)(NSDictionary *);
  void (^_dataCallback)(id);
  void (^_closeCallback)(NSDictionary *, NSError *);
  void (^_writeMessageCallback)(void);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                   dataCallback:(void (^)(id))dataCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback
                           writeMessageCallback:(void (^)(void))writeMessageCallback {
  if ((self = [super init])) {
    _initialMetadataCallback = initialMetadataCallback;
    _dataCallback = dataCallback;
    _closeCallback = closeCallback;
    _writeMessageCallback = writeMessageCallback;
    _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (_initialMetadataCallback) {
    _initialMetadataCallback(initialMetadata);
  }
}

- (void)didReceiveProtoMessage:(id)message {
  if (_dataCallback) {
    _dataCallback(message);
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (_closeCallback) {
    _closeCallback(trailingMetadata, error);
  }
}

- (void)didWriteMessage {
  if (_writeMessageCallback) {
    _writeMessageCallback();
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}
@end

@interface TransportTests : XCTestCase

@end

@implementation TransportTests

- (void)testTransportInterceptors {
  __weak XCTestExpectation *expectComplete =
      [self expectationWithDescription:@"Expect call complete"];
  [GRPCFakeTransportFactory sharedInstance].nextTransportInstance = nil;

  PhonyInterceptorFactory *factory = [[PhonyInterceptorFactory alloc] initWithPassthrough:YES];
  PhonyInterceptorFactory *factory2 = [[PhonyInterceptorFactory alloc] initWithPassthrough:NO];
  [[GRPCFakeTransportFactory sharedInstance]
      setTransportInterceptorFactories:@[ factory, factory2 ]];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kRemoteHost
                                          path:@"/UnaryCall"
                                        safety:GRPCCallSafetyDefault];
  GRPCMutableCallOptions *callOptions = [[GRPCMutableCallOptions alloc] init];
  callOptions.transport = kFakeTransportID;
  GRPCCall2 *call = [[GRPCCall2 alloc]
      initWithRequestOptions:requestOptions
             responseHandler:[[TestsBlockCallbacks alloc]
                                 initWithInitialMetadataCallback:nil
                                                    dataCallback:nil
                                                   closeCallback:^(NSDictionary *trailingMetadata,
                                                                   NSError *error) {
                                                     XCTAssertNotNil(error);
                                                     XCTAssertEqual(error.code,
                                                                    GRPCErrorCodeCancelled);
                                                     [expectComplete fulfill];
                                                   }
                                            writeMessageCallback:nil]
                 callOptions:callOptions];
  [call start];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
  XCTAssertTrue(factory.lastInterceptor.hit);
  XCTAssertTrue(factory2.lastInterceptor.hit);
}

@end
