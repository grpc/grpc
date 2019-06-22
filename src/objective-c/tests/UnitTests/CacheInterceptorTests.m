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

#import <XCTest/XCTest.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCInterceptor.h>
#import <RemoteTest/Messages.pbobjc.h>
#import "../../GRPCClient/CacheInterceptor.h"
#import "../../GRPCClient/private/GRPCCallInternal.h"

#define TEST_TIMEOUT 2
#define TEST_DUMMY_DATA @"google"
#define NUMBER(x) [NSNumber numberWithInt:x]

/**
 * Mocking InterceptorManager
 */
@interface TestInterceptorManager : GRPCInterceptorManager

@property(readwrite) __weak GRPCInterceptor *interceptor;
@property(readonly) NSUInteger callThruCount;
@property(readwrite) NSMutableDictionary *headerChecker;
@property(readonly) BOOL headerCheckPassed;
@property(readwrite) BOOL useOriginalShutDown;

- (instancetype)init;

@end

@implementation TestInterceptorManager {
  __weak GRPCInterceptor *_interceptor;
  NSDictionary *_responseHeaders;
  id _responseData;
  NSDictionary *_responseTrailers;
  NSError *_responseError;
  NSUInteger _callThruCount;
  void (^_requestReceivedHandler)(void);
  
  NSMutableDictionary *_headerChecker;
  BOOL _headerCheckPassed;
  BOOL _realShutDdown;
}

@synthesize callThruCount = _callThruCount;
@synthesize headerCheckPassed = _headerCheckPassed;
@synthesize useOriginalShutDown = _realShutDdown;

- (instancetype)init {
  GRPCCall2Internal *nextInterceptor = [[GRPCCall2Internal alloc] init];
  self = [super initWithNextInterceptor:nextInterceptor];
  _callThruCount = 0;
  _responseData = TEST_DUMMY_DATA;
  _headerChecker = [[NSMutableDictionary alloc] init];
  _headerCheckPassed = YES;
  return self;
}

- (void)setResponseHeaders:(NSDictionary *)metaData {
  _responseHeaders = metaData;
}

- (void)setResponseData:(id)data {
  _responseData = data;
}

- (void)setResponseTrailers:(NSDictionary *)metaData {
  _responseTrailers = metaData;
}

- (void)setResponseError:(NSError *)error {
  _responseError = error;
}

- (void)setRequestReceivedHandler:(void (^)(void))handler {
  _requestReceivedHandler = handler;
}

- (void)startNextInterceptorWithRequest:(GRPCRequestOptions *)requestOptions
                            callOptions:(GRPCCallOptions *)callOptions {
  NSDictionary *metadata = callOptions.initialMetadata;
  for (NSString *key in _headerChecker) {
    NSString *entry = [metadata objectForKey:key];
    if (!entry || ![entry isEqualToString:_headerChecker[key]]) {
      _headerCheckPassed = NO;
      return;
    }
  }
}

- (void)writeNextInterceptorWithData:(id)data {
  
}

- (void)finishNextInterceptor {
  ++_callThruCount;
  if (_requestReceivedHandler) {
    _requestReceivedHandler();
  }
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didReceiveInitialMetadata:_responseHeaders];
  });
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didReceiveData:_responseData];
  });
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didCloseWithTrailingMetadata:_responseTrailers error:_responseError];
  });
}

- (void)shutDown {
  // Does not set the next/prev interceptors to nil in order to reuse the manager
  if (_realShutDdown) {
    [super shutDown];
  }
}

@end

/**
 * Test main class & response handler
 */
@interface CacheInterceptorTests : XCTestCase<GRPCResponseHandler>

@end

@interface CacheInterceptorTests()

- (void)makeCallWithData:(id)data;
- (void)makeCallWithData:(id)data forInterceptor:(GRPCInterceptor*)interceptor;

@end

@implementation CacheInterceptorTests {
  TestInterceptorManager *_manager;
  CacheContext *_context;
  NSDictionary *_initialMetadata;
  id _data;
  NSDictionary *_trailingMetadata;
  NSError *_error;
  GRPCRequestOptions *_requestOptions;
  GRPCMutableCallOptions *_callOptions;
  __weak XCTestExpectation *_callCompleteExpectation;
}

/**
 * Private Helper functions
 */
- (void)makeCallWithData:(id)data {
  _callCompleteExpectation = [self expectationWithDescription:@"Received response."];
  GRPCInterceptor *interceptor = [_context createInterceptorWithManager:_manager];
  _manager.interceptor = interceptor;
  [self makeCallWithData:data forInterceptor:interceptor];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)makeCallWithData:(id)data forInterceptor:(GRPCInterceptor *)interceptor {
  [interceptor startWithRequestOptions:_requestOptions callOptions:_callOptions];
  [interceptor writeData:data];
  [interceptor finish];
}

/**
 * XCTest Preparation functions
 */
- (void)setUp {
  _manager = [[TestInterceptorManager alloc] init];
  [_manager setPreviousInterceptor:self];
  _context = [[CacheContext alloc] initWithSize:5];
  _requestOptions = [[GRPCRequestOptions alloc]
                                        initWithHost:@"does not matter"
                                        path:@"does not matter either"
                                        safety:GRPCCallSafetyCacheableRequest];
  id<GRPCInterceptorFactory> factory = _context;
  _callOptions = [[GRPCMutableCallOptions alloc] init];
  _callOptions.interceptorFactories = @[ factory ];
  
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=10" }; // default to cacheable header
}

- (void)tearDown { /* might not be needed */ }

/**
 * ResponseHandlerInterface
 */
- (dispatch_queue_t) dispatchQueue {
 return dispatch_get_main_queue();
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  _initialMetadata = initialMetadata;
}

- (void)didReceiveData:(id)data {
  _data = data;
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  _trailingMetadata = trailingMetadata;
  _error = error;
  [_callCompleteExpectation fulfill];
}

/**
 * TESTS
 */
- (void)test0 {
  _manager.responseData = @"2";
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual((NSString *)_data, @"2");
}

- (void)testCancelCallBeforeRequestSent {
  _manager.useOriginalShutDown = YES;
  _callCompleteExpectation = [self expectationWithDescription:@"Received response."];
  GRPCInterceptor *interceptor = [_context createInterceptorWithManager:_manager];
  _manager.interceptor = interceptor;
  [interceptor startWithRequestOptions:_requestOptions callOptions:_callOptions];
  [interceptor writeData:TEST_DUMMY_DATA];
  [interceptor cancel];
  [interceptor finish];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
  XCTAssertNotNil(_error);
}

- (void)testCancelCallAfterRequestSent {
  _manager.useOriginalShutDown = YES;
  _callCompleteExpectation = [self expectationWithDescription:@"Received response."];
  GRPCInterceptor *interceptor = [_context createInterceptorWithManager:_manager];
  _manager.interceptor = interceptor;
  _manager.requestReceivedHandler = ^{
    dispatch_async(interceptor.dispatchQueue, ^{
      [interceptor cancel];
    });
  };
  _manager.responseError = [NSError errorWithDomain:@"io.test" code:3 userInfo:@{ @"msg": @"Error for test" }];
  [interceptor startWithRequestOptions:_requestOptions callOptions:_callOptions];
  [interceptor writeData:TEST_DUMMY_DATA];
  [interceptor finish];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
  XCTAssertNotNil(_error);
  XCTAssertTrue([_error.domain isEqualToString:@"io.grpc"]);
}

- (void)testCancelCallAfterForwardingResponse {
  _manager.useOriginalShutDown = YES;
  _callCompleteExpectation = [self expectationWithDescription:@"Received response."];
  GRPCInterceptor *interceptor = [_context createInterceptorWithManager:_manager];
  _manager.interceptor = interceptor;
  [interceptor startWithRequestOptions:_requestOptions callOptions:_callOptions];
  [interceptor writeData:TEST_DUMMY_DATA];
  [interceptor finish];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
  [interceptor cancel];
  XCTAssertNil(_error);
}

- (void)testNoStoreHeader {
  _manager.responseHeaders = @{ @"cache-control": @"no-store" };
  
  for (int i = 0; i < 5; ++i) {
    [self makeCallWithData:TEST_DUMMY_DATA];
  }
  XCTAssertEqual(5, _manager.callThruCount);
}

- (void)testCacheLimit {
  // There is an assert statement in this file: ../../GRPCClient/CacheInterceptor.m
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=5" };
  for (int i = 0; i < 6; ++i) {
    [self makeCallWithData:NUMBER(i)];
  }
}


- (void)testLRUEviction {
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=5" };
  for (int i = 1; i <= 6; ++i) {
    [self makeCallWithData:NUMBER(i)];
  }
  // queue: 6 5 4 3 2 (1 evicted)
  XCTAssertEqual(6, _manager.callThruCount);
  [self makeCallWithData:NUMBER(1)];
  // queue: 1 6 5 4 3 (2 evicted)
  XCTAssertEqual(7, _manager.callThruCount);
  [self makeCallWithData:NUMBER(3)]; // 3 1 6 5 4
  XCTAssertEqual(7, _manager.callThruCount);
  [self makeCallWithData:NUMBER(2)]; // 2 3 1 6 5
  XCTAssertEqual(8, _manager.callThruCount);
  [self makeCallWithData:NUMBER(3)]; // 3 2 1 6 5
  XCTAssertEqual(8, _manager.callThruCount);
  [self makeCallWithData:NUMBER(6)]; // 6 3 2 1 5
  XCTAssertEqual(8, _manager.callThruCount);
}

- (void)testSmallCacheSize1 {
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=5" };
  
  _context = [[CacheContext alloc] initWithSize:1];
  id<GRPCInterceptorFactory> factory = _context;
  _callOptions = [[GRPCMutableCallOptions alloc] init];
  _callOptions.interceptorFactories = @[ factory ];
  
  for (int i = 0; i < 5; ++i) {
    [self makeCallWithData:NUMBER(1)];
  }
  XCTAssertEqual(1, _manager.callThruCount);
  [self makeCallWithData:NUMBER(2)];
  XCTAssertEqual(2, _manager.callThruCount);
  [self makeCallWithData:NUMBER(1)];
  XCTAssertEqual(3, _manager.callThruCount);
  [self makeCallWithData:NUMBER(1)];
  XCTAssertEqual(3, _manager.callThruCount);
}

- (void)testSmallCacheSize2 {
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=5" };
  
  _context = [[CacheContext alloc] initWithSize:2];
  id<GRPCInterceptorFactory> factory = _context;
  _callOptions = [[GRPCMutableCallOptions alloc] init];
  _callOptions.interceptorFactories = @[ factory ];
  
  for (int i = 0; i < 3; ++i) {
    [self makeCallWithData:NUMBER(1)];
  }
  for (int i = 0; i < 3; ++i) {
    [self makeCallWithData:NUMBER(2)];
  }
  XCTAssertEqual(2, _manager.callThruCount);
  for (int i = 0; i < 3; ++i) {
    [self makeCallWithData:NUMBER(1)];
    [self makeCallWithData:NUMBER(2)];
  }
  XCTAssertEqual(2, _manager.callThruCount);
  
  [self makeCallWithData:NUMBER(3)];
  [self makeCallWithData:NUMBER(2)];
  [self makeCallWithData:NUMBER(1)];
  [self makeCallWithData:NUMBER(3)];
  XCTAssertEqual(5, _manager.callThruCount);
}

- (void)testMaxAge {
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=1" };
  [self makeCallWithData:TEST_DUMMY_DATA];
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual(1, _manager.callThruCount);
  sleep(2);
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual(2, _manager.callThruCount);
}

- (void)testMaxAgeWithOtherRequests {
  for (int i = 1; i <= 5; ++i) {
    if (i == 3) {
      _manager.responseHeaders = @{ @"cache-control": @"public, max-age=1" };
    } else {
      _manager.responseHeaders = @{ @"cache-control": @"private, max-age=3600" };
    }
    [self makeCallWithData:NUMBER(i)];
  }
  XCTAssertEqual(5, _manager.callThruCount);
  [self makeCallWithData:NUMBER(3)];
  XCTAssertEqual(5, _manager.callThruCount);
  [self makeCallWithData:NUMBER(6)];
  XCTAssertEqual(6, _manager.callThruCount);
  sleep(1);
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=1" };
  [self makeCallWithData:NUMBER(3)];
  XCTAssertEqual(7, _manager.callThruCount);
  [self makeCallWithData:NUMBER(5)];
  XCTAssertEqual(7, _manager.callThruCount);
}

- (void)testETagHeader {
  _manager.responseHeaders = @{ @"cache-control": @"public, max-age=1",
                                @"etag": @"googleLLC",
                                @"status": @"200"
                                };
  [self makeCallWithData:TEST_DUMMY_DATA];
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual(1, _manager.callThruCount);
  
  sleep(1);
  _manager.headerChecker[@"if-none-match"] = @"googleLLC";
  _manager.responseHeaders = @{ @"status": @"304" };
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertTrue(_manager.headerCheckPassed);
  XCTAssertEqual(2, _manager.callThruCount);
  XCTAssertTrue([TEST_DUMMY_DATA isEqualToString:(NSString *)_data]);
  XCTAssertTrue([_initialMetadata[@"status"] isEqualToString:@"200"]);
  
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual(2, _manager.callThruCount);
}

- (void)testLastModifiedHeader {
  _manager.responseHeaders = @{ @"cache-control": @"private, max-age=1",
                                @"last-modified": @"Mon, 17 Jun 2019 18:00:00 GMT",
                                @"status": @"200"
                                };
  [self makeCallWithData:TEST_DUMMY_DATA];
  
  _manager.headerChecker[@"if-modified-since"] = @"Mon, 17 Jun 2019 18:00:00 GMT";
  sleep(1);
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertTrue(_manager.headerCheckPassed);
  XCTAssertEqual(2, _manager.callThruCount);
  _manager.responseHeaders = @{ @"cache-control": @"private, max-age=1",
                                @"date": @"Mon, 17 Jun 2019 19:00:00 GMT",
                                @"status": @"200"
                                };
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertEqual(2, _manager.callThruCount); // still using cache
  sleep(1);
  [self makeCallWithData:TEST_DUMMY_DATA]; // now last-modified becomes 19:00 (date header)
  XCTAssertEqual(3, _manager.callThruCount);
  
  sleep(1);
  _manager.headerChecker[@"if-modified-since"] = @"Mon, 17 Jun 2019 19:00:00 GMT";
  [self makeCallWithData:TEST_DUMMY_DATA];
  XCTAssertTrue(_manager.headerCheckPassed);
}

@end
