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
#import "../../GRPCClient/CacheInterceptor.h"
#import "../../GRPCClient/private/GRPCCallInternal.h"


/**
 * Mocking InterceptorManager
 */
@interface TestInterceptorManager : GRPCInterceptorManager

- (instancetype)init;

@end

@implementation TestInterceptorManager

- (instancetype)init {
  GRPCCall2Internal *nextInterceptor = [[GRPCCall2Internal alloc] init];
  self = [super initWithNextInterceptor:nextInterceptor];
  return self;
}

@end

/**
 * Test main class
 */
@interface CacheInterceptorTests : XCTestCase

@end

@implementation CacheInterceptorTests {
  GRPCInterceptor *_interceptor;
  TestInterceptorManager *_manager;
}

- (void)setUp {
  _manager = [[TestInterceptorManager alloc] init];
  _interceptor = [[[CacheContext alloc] init] createInterceptorWithManager:_manager];
}

- (void)tearDown {
}

- (void)test1 { // needs renaming later
}

@end
