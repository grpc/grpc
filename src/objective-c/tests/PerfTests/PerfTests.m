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

#import "PerfTests.h"

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

#import "PerfTestsBlockCallbacks.h"

#define TEST_TIMEOUT 128

extern const char *kCFStreamVarName;

// Convenience constructors for the generated proto messages:

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

#pragma mark Tests

@implementation PerfTests {
  RMTTestService *_service;
}

+ (XCTestSuite *)defaultTestSuite {
  if (self == [PerfTests class]) {
    return [XCTestSuite testSuiteWithName:@"PerfTestsEmptySuite"];
  } else {
    return super.defaultTestSuite;
  }
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

+ (GRPCTransportID)transport {
  return NULL;
}

+ (NSString *)PEMRootCertificates {
  return nil;
}

+ (NSString *)hostNameOverride {
  return nil;
}

- (void)setUp {
  self.continueAfterFailure = NO;

  [GRPCCall resetHostSettings];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [GRPCCall closeOpenConnections];
#pragma clang diagnostic pop

  _service = [[self class] host] ? [RMTTestService serviceWithHost:[[self class] host]] : nil;
}

- (BOOL)isUsingCFStream {
  return [NSStringFromClass([self class]) isEqualToString:@"PerfTestsCFStreamSSL"];
}

- (void)pingPongV2APIWithRequest:(RMTStreamingOutputCallRequest *)request
                     numMessages:(int)numMessages
                         options:(GRPCMutableCallOptions *)options {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPong"];

  __block BOOL flowControlEnabled = options.flowControlEnabled;
  __block int index = 0;
  __block GRPCStreamingProtoCall *call = [self->_service
      fullDuplexCallWithResponseHandler:[[PerfTestsBlockCallbacks alloc]
                                            initWithInitialMetadataCallback:nil
                                            messageCallback:^(id message) {
                                              int indexCopy;
                                              @synchronized(self) {
                                                indexCopy = index;
                                                index += 1;
                                              }
                                              if (indexCopy < numMessages) {
                                                [call writeMessage:request];
                                                if (flowControlEnabled) {
                                                  [call receiveNextMessage];
                                                }
                                              } else {
                                                [call finish];
                                              }
                                            }
                                            closeCallback:^(NSDictionary *trailingMetadata,
                                                            NSError *error) {
                                              [expectation fulfill];
                                            }]
                            callOptions:options];
  [call start];
  if (flowControlEnabled) {
    [call receiveNextMessage];
  }
  [call writeMessage:request];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testPingPongRPCWithV2API {
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];

  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@1 requestedResponseSize:@1];

  // warm up
  [self pingPongV2APIWithRequest:request numMessages:1000 options:options];

  [self measureBlock:^{
    [self pingPongV2APIWithRequest:request numMessages:1000 options:options];
  }];
}

- (void)testPingPongRPCWithFlowControl {
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];
  options.flowControlEnabled = YES;

  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@1 requestedResponseSize:@1];

  // warm up
  [self pingPongV2APIWithRequest:request numMessages:1000 options:options];

  [self measureBlock:^{
    [self pingPongV2APIWithRequest:request numMessages:1000 options:options];
  }];
}

- (void)testPingPongRPCWithInterceptor {
  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];
  options.interceptorFactories = @[ [[DefaultInterceptorFactory alloc] init] ];

  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@1 requestedResponseSize:@1];

  // warm up
  [self pingPongV2APIWithRequest:request numMessages:1000 options:options];

  [self measureBlock:^{
    [self pingPongV2APIWithRequest:request numMessages:1000 options:options];
  }];
}

- (void)pingPongV1APIWithRequest:(RMTStreamingOutputCallRequest *)request
                     numMessages:(int)numMessages {
  __block int index = 0;
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"PingPong"];
  GRXBufferedPipe *requestsBuffer = [[GRXBufferedPipe alloc] init];

  [requestsBuffer writeValue:request];

  [_service fullDuplexCallWithRequestsWriter:requestsBuffer
                                eventHandler:^(BOOL done, RMTStreamingOutputCallResponse *response,
                                               NSError *error) {
                                  if (response) {
                                    int indexCopy;
                                    @synchronized(self) {
                                      index += 1;
                                      indexCopy = index;
                                    }
                                    if (indexCopy < numMessages) {
                                      [requestsBuffer writeValue:request];
                                    } else {
                                      [requestsBuffer writesFinishedWithError:nil];
                                    }
                                  }

                                  if (done) {
                                    [expectation fulfill];
                                  }
                                }];
  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)testPingPongRPCWithV1API {
  id request = [RMTStreamingOutputCallRequest messageWithPayloadSize:@1 requestedResponseSize:@1];
  [self pingPongV1APIWithRequest:request numMessages:1000];
  [self measureBlock:^{
    [self pingPongV1APIWithRequest:request numMessages:1000];
  }];
}

- (void)unaryRPCsWithServices:(NSArray<RMTTestService *> *)services
                      request:(RMTSimpleRequest *)request
              callsPerService:(int)callsPerService
          maxOutstandingCalls:(int)maxOutstandingCalls
                  callOptions:(GRPCMutableCallOptions *)options {
  __weak XCTestExpectation *expectation = [self expectationWithDescription:@"unaryRPC"];

  dispatch_semaphore_t sema = dispatch_semaphore_create(maxOutstandingCalls);
  __block int index = 0;

  for (RMTTestService *service in services) {
    for (int i = 0; i < callsPerService; ++i) {
      GRPCUnaryProtoCall *call = [service
          unaryCallWithMessage:request
               responseHandler:[[PerfTestsBlockCallbacks alloc]
                                   initWithInitialMetadataCallback:nil
                                                   messageCallback:nil
                                                     closeCallback:^(NSDictionary *trailingMetadata,
                                                                     NSError *error) {
                                                       dispatch_semaphore_signal(sema);
                                                       @synchronized(self) {
                                                         ++index;
                                                         if (index ==
                                                             callsPerService * [services count]) {
                                                           [expectation fulfill];
                                                         }
                                                       }
                                                     }]
                   callOptions:options];
      dispatch_time_t timeout =
          dispatch_time(DISPATCH_TIME_NOW, (int64_t)(TEST_TIMEOUT * NSEC_PER_SEC));
      dispatch_semaphore_wait(sema, timeout);
      [call start];
    }
  }

  [self waitForExpectationsWithTimeout:TEST_TIMEOUT handler:nil];
}

- (void)test1MBUnaryRPC {
  // Workaround Apple CFStream bug
  if ([self isUsingCFStream]) {
    return;
  }

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 1048576;
  request.payload.body = [NSMutableData dataWithLength:1048576];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];

  // warm up
  [self unaryRPCsWithServices:@[ self->_service ]
                      request:request
              callsPerService:50
          maxOutstandingCalls:10
                  callOptions:options];

  [self measureBlock:^{
    [self unaryRPCsWithServices:@[ self->_service ]
                        request:request
                callsPerService:50
            maxOutstandingCalls:10
                    callOptions:options];
  }];
}

- (void)test1KBUnaryRPC {
  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 1024;
  request.payload.body = [NSMutableData dataWithLength:1024];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];

  // warm up
  [self unaryRPCsWithServices:@[ self->_service ]
                      request:request
              callsPerService:1000
          maxOutstandingCalls:100
                  callOptions:options];

  [self measureBlock:^{
    [self unaryRPCsWithServices:@[ self->_service ]
                        request:request
                callsPerService:1000
            maxOutstandingCalls:100
                    callOptions:options];
  }];
}

- (void)testMultipleChannels {
  NSString *port = [[[self class] host] componentsSeparatedByString:@":"][1];
  int kNumAddrs = 10;
  NSMutableArray<NSString *> *addrs = [NSMutableArray arrayWithCapacity:kNumAddrs];
  NSMutableArray<RMTTestService *> *services = [NSMutableArray arrayWithCapacity:kNumAddrs];
  for (int i = 0; i < kNumAddrs; ++i) {
    addrs[i] = [NSString stringWithFormat:@"127.0.0.%d", (i + 1)];
    NSString *hostWithPort = [NSString stringWithFormat:@"%@:%@", addrs[i], port];
    services[i] = [RMTTestService serviceWithHost:hostWithPort];
  }

  RMTSimpleRequest *request = [RMTSimpleRequest message];
  request.responseSize = 0;
  request.payload.body = [NSMutableData dataWithLength:0];

  GRPCMutableCallOptions *options = [[GRPCMutableCallOptions alloc] init];
  options.transport = [[self class] transport];
  options.PEMRootCertificates = [[self class] PEMRootCertificates];
  options.hostNameOverride = [[self class] hostNameOverride];

  // warm up
  [self unaryRPCsWithServices:services
                      request:request
              callsPerService:100
          maxOutstandingCalls:100
                  callOptions:options];

  [self measureBlock:^{
    [self unaryRPCsWithServices:services
                        request:request
                callsPerService:100
            maxOutstandingCalls:100
                    callOptions:options];
  }];
}

@end
