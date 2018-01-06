//
//  test.m
//  test
//
//  Created by Muxi Yan on 12/8/17.
//  Copyright © 2017 gRPC. All rights reserved.
//



extern int main_server_context_test_spouse_test(int argc, char** argv);

#import <XCTest/XCTest.h>

@interface test : XCTestCase

@end

@implementation test

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testExample {
    // This is an example of a functional test case.
    // Use XCTAssert and related functions to verify your tests produce the correct results.
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
