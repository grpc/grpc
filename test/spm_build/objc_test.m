/*
 *
 * Copyright 2026 gRPC authors.
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

#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>
#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCCallOptions.h>
#import <GRPCClient/GRPCTransport.h>
#import <ProtoRPC/ProtoMethod.h>
#import <ProtoRPC/ProtoRPC.h>
#import <ProtoRPC/ProtoService.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <RxLibrary/GRXWriter.h>
#include <grpc/grpc.h>

@interface SPMPackageTests : XCTestCase
@end

@implementation SPMPackageTests

- (void)testGRPCCoreVersion {
  const char *version = grpc_version_string();
  XCTAssertTrue(version != NULL && strlen(version) > 0);
}

- (void)testRxLibraryWriter {
  XCTestExpectation *exp = [self expectationWithDescription:@"GRXWriter value delivered"];
  NSData *payload = [@"hello" dataUsingEncoding:NSUTF8StringEncoding];
  GRXWriter *writer = [GRXWriter writerWithValue:payload];
  XCTAssertNotNil(writer);
  [writer startWithWriteable:[GRXWriteable writeableWithSingleHandler:^(id value, NSError *error) {
            XCTAssertNil(error);
            XCTAssertEqualObjects(value, payload);
            [exp fulfill];
          }]];
  [self waitForExpectations:@[ exp ] timeout:5.0];
}

- (void)testGRPCClientCallOptions {
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = GRPCDefaultTransportImplList.core_secure;
  options.timeout = 10.0;
  GRPCCallOptions *immutable = [options copy];
  XCTAssertEqual(immutable.timeout, 10.0);
  XCTAssertEqual(immutable.transport, GRPCDefaultTransportImplList.core_secure);
}

- (void)testProtoRPCMethodAndService {
  GRPCProtoMethod *method = [[GRPCProtoMethod alloc] initWithPackage:@"grpc.testing"
                                                             service:@"TestService"
                                                              method:@"UnaryCall"];
  XCTAssertEqualObjects(method.HTTPPath, @"/grpc.testing.TestService/UnaryCall");

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = GRPCDefaultTransportImplList.core_secure;
  GRPCProtoService *service = [[GRPCProtoService alloc] initWithHost:@"grpc-test.sandbox.googleapis.com"
                                                         packageName:@"grpc.testing"
                                                         serviceName:@"TestService"
                                                         callOptions:options];
  XCTAssertNotNil(service);
}

@end
