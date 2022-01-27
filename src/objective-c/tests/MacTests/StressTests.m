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
#include "StressTests.h"

#import <GRPCClient/GRPCCall+ChannelArg.h>
#import <GRPCClient/GRPCCall+Tests.h>
#import <GRPCClient/internal_testing/GRPCCall+InternalTests.h>
#import <ProtoRPC/ProtoRPC.h>
#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#import <grpc/grpc.h>
#import <grpc/support/log.h>
#import "src/objective-c/tests/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbobjc.h"
#import "src/objective-c/tests/RemoteTestClient/Test.pbrpc.h"

#define TEST_TIMEOUT 32

extern const char *kCFStreamVarName;

// Convenience class to use blocks as callbacks
@interface MacTestsBlockCallbacks : NSObject <GRPCProtoResponseHandler>

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback;

@end

@implementation MacTestsBlockCallbacks {
  void (^_initialMetadataCallback)(NSDictionary *);
  void (^_messageCallback)(id);
  void (^_closeCallback)(NSDictionary *, NSError *);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback {
  if ((self = [super init])) {
    _initialMetadataCallback = initialMetadataCallback;
    _messageCallback = messageCallback;
    _closeCallback = closeCallback;
    _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (_initialMetadataCallback) {
    _initialMetadataCallback(initialMetadata);
  }
}

- (void)didReceiveProtoMessage:(GPBMessage *)message {
  if (_messageCallback) {
    _messageCallback(message);
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (_closeCallback) {
    _closeCallback(trailingMetadata, error);
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end

@implementation StressTests {
  RMTTestService *_service;
}

+ (XCTestSuite *)defaultTestSuite {
  if (self == [StressTests class]) {
    return [XCTestSuite testSuiteWithName:@"StressTestsEmptySuite"];
  } else {
    return super.defaultTestSuite;
  }
}

+ (NSString *)host {
  return nil;
}

+ (NSString *)hostAddress {
  return nil;
}

+ (NSString *)PEMRootCertificates {
  return nil;
}

+ (NSString *)hostNameOverride {
  return nil;
}

- (int32_t)encodingOverhead {
  return 0;
}

+ (void)setUp {
  setenv(kCFStreamVarName, "1", 1);
}

- (void)setUp {
  self.continueAfterFailure = NO;

  [GRPCCall resetHostSettings];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [GRPCCall closeOpenConnections];
#pragma clang diagnostic pop

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transportType = [[self class] transportType];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];
  _service = [RMTTestService serviceWithHost:[[self class] host] callOptions:options];
  system([[NSString stringWithFormat:@"sudo ifconfig lo0 alias %@", [[self class] hostAddress]]
      UTF8String]);
}

- (void)tearDown {
  system([[NSString stringWithFormat:@"sudo ifconfig lo0 -alias %@", [[self class] hostAddress]]
      UTF8String]);
}

+ (GRPCTransportType)transportType {
  return GRPCTransportTypeChttp2BoringSSL;
}

- (void)testNetworkFlapWithV2API {
  NSMutableArray *completeExpectations = [NSMutableArray array];
  NSMutableArray *calls = [NSMutableArray array];
  int num_rpcs = 100;
  __block BOOL address_removed = FALSE;
  __block BOOL address_readded = FALSE;
  for (int i = 0; i < num_rpcs; ++i) {
    [completeExpectations
        addObject:[self expectationWithDescription:
                            [NSString stringWithFormat:@"Received trailer for RPC %d", i]]];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = 314159;
    request.payload.body = [NSMutableData dataWithLength:271828];

    GRPCUnaryProtoCall *call = [_service
        unaryCallWithMessage:request
             responseHandler:[[MacTestsBlockCallbacks alloc] initWithInitialMetadataCallback:nil
                                 messageCallback:^(id message) {
                                   if (message) {
                                     RMTSimpleResponse *expectedResponse =
                                         [RMTSimpleResponse message];
                                     expectedResponse.payload.type = RMTPayloadType_Compressable;
                                     expectedResponse.payload.body =
                                         [NSMutableData dataWithLength:314159];
                                     XCTAssertEqualObjects(message, expectedResponse);
                                   }
                                 }
                                 closeCallback:^(NSDictionary *trailingMetadata, NSError *error) {
                                   @synchronized(self) {
                                     if (error == nil && !address_removed) {
                                       system([[NSString
                                           stringWithFormat:@"sudo ifconfig lo0 -alias %@",
                                                            [[self class] hostAddress]]
                                           UTF8String]);
                                       address_removed = YES;
                                     } else if (error != nil && !address_readded) {
                                       system([[NSString
                                           stringWithFormat:@"sudo ifconfig lo0 alias %@",
                                                            [[self class] hostAddress]]
                                           UTF8String]);
                                       address_readded = YES;
                                     }
                                   }
                                   [completeExpectations[i] fulfill];
                                 }]
                 callOptions:nil];
    [calls addObject:call];
  }

  for (int i = 0; i < num_rpcs; ++i) {
    GRPCUnaryProtoCall *call = calls[i];
    [call start];
    [NSThread sleepForTimeInterval:0.1f];
  }
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testNetworkFlapWithV1API {
  NSMutableArray *completeExpectations = [NSMutableArray array];
  int num_rpcs = 100;
  __block BOOL address_removed = FALSE;
  __block BOOL address_readded = FALSE;
  for (int i = 0; i < num_rpcs; ++i) {
    [completeExpectations
        addObject:[self expectationWithDescription:
                            [NSString stringWithFormat:@"Received response for RPC %d", i]]];

    RMTSimpleRequest *request = [RMTSimpleRequest message];
    request.responseType = RMTPayloadType_Compressable;
    request.responseSize = 314159;
    request.payload.body = [NSMutableData dataWithLength:271828];

    [_service unaryCallWithRequest:request
                           handler:^(RMTSimpleResponse *response, NSError *error) {
                             @synchronized(self) {
                               if (error == nil && !address_removed) {
                                 system([[NSString stringWithFormat:@"sudo ifconfig lo0 -alias %@",
                                                                    [[self class] hostAddress]]
                                     UTF8String]);
                                 address_removed = YES;
                               } else if (error != nil && !address_readded) {
                                 system([[NSString stringWithFormat:@"sudo ifconfig lo0 alias %@",
                                                                    [[self class] hostAddress]]
                                     UTF8String]);
                                 address_readded = YES;
                               }
                             }

                             [completeExpectations[i] fulfill];
                           }];

    [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
  }
}

@end
