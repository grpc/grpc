//
//  CacheInterceptorTests.m
//  UnitTests
//
//  Created by Tony Lu on 6/12/19.
//  Copyright © 2019 gRPC. All rights reserved.
//

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
- (instancetype)init;

@end

@implementation TestInterceptorManager {
  __weak GRPCInterceptor *_interceptor;
  NSDictionary *_responseInitialMetadata;
  id _responseData;
  NSDictionary *_responseTrailingMetadata;
  NSUInteger _callThruCount;
}

@synthesize callThruCount = _callThruCount;

- (instancetype)init {
  GRPCCall2Internal *nextInterceptor = [[GRPCCall2Internal alloc] init];
  self = [super initWithNextInterceptor:nextInterceptor];
  _callThruCount = 0;
  _responseData = TEST_DUMMY_DATA;
  return self;
}

- (void)setResponseInitialMetadata:(NSDictionary *)metaData {
  _responseInitialMetadata = metaData;
}

- (void)setResponseData:(id)data {
  _responseData = data;
}

- (void)setResponseTrailingMetadata:(NSDictionary *)metaData {
  _responseTrailingMetadata = metaData;
}

- (void)startNextInterceptorWithRequest:(GRPCRequestOptions *)requestOptions
                            callOptions:(GRPCCallOptions *)callOptions {
}

- (void)writeNextInterceptorWithData:(id)data {
  
}

- (void)finishNextInterceptor {
  ++_callThruCount;
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didReceiveInitialMetadata:_responseInitialMetadata];
  });
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didReceiveData:_responseData];
  });
  dispatch_async(_interceptor.dispatchQueue, ^{
    [_interceptor didCloseWithTrailingMetadata:_responseTrailingMetadata error:nil];
  });
}

- (void)shutDown {
  
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

- (void)testNoStoreHeader {
  _manager.responseInitialMetadata = @{ @"cache-control": @"no-store" };
  
  for (int i = 0; i < 5; ++i) {
    [self makeCallWithData:TEST_DUMMY_DATA];
  }
  XCTAssertEqual(5, _manager.callThruCount);
}

- (void)testCacheableHeader {
  _manager.responseInitialMetadata = @{ @"cache-control": @"private" }; // can be omitted
  
  for (int i = 0; i < 5; ++i) {
    [self makeCallWithData:TEST_DUMMY_DATA];
  }
  XCTAssertEqual(1, _manager.callThruCount);
}

- (void)testCacheLimit {
  // There is an assert statement in this file: ../../GRPCClient/CacheInterceptor.m
  for (int i = 0; i < 6; ++i) {
    [self makeCallWithData:NUMBER(i)];
  }
}


- (void)testLruEviction {
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
}

- (void)testSmallCacheSize2 {
  _context = [[CacheContext alloc] initWithSize:2];
  id<GRPCInterceptorFactory> factory = _context;
  _callOptions = [[GRPCMutableCallOptions alloc] init];
  _callOptions.interceptorFactories = @[ factory ];
  
  for (int i = 0; i < 5; ++i) {
    [self makeCallWithData:NUMBER(1)];
  }
  for (int i = 0; i < 5; ++i) {
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

@end
