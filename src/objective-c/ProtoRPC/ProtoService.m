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

#import "ProtoService.h"

#import <GRPCClient/GRPCCall.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter.h>

#import "ProtoMethod.h"
#import "ProtoRPC.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
@implementation ProtoService {
#pragma clang diagnostic pop
  NSString *_host;
  NSString *_packageName;
  NSString *_serviceName;
  GRPCCallOptions *_callOptions;
}

- (instancetype)init {
  return [self initWithHost:nil packageName:nil serviceName:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName
                 callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(host.length != 0 && packageName.length != 0 && serviceName.length != 0,
           @"Invalid parameter.");
  if (host.length == 0 || packageName.length == 0 || serviceName.length == 0) {
    return nil;
  }
  if ((self = [super init])) {
    _host = [host copy];
    _packageName = [packageName copy];
    _serviceName = [serviceName copy];
    _callOptions = [callOptions copy];
  }
  return self;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-designated-initializers"
// Do not call designated initializer here due to nullability incompatibility. This method is from
// old API and does not assert on nullability of the parameters.

- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName {
  if ((self = [super init])) {
    _host = [host copy];
    _packageName = [packageName copy];
    _serviceName = [serviceName copy];
    _callOptions = nil;
  }
  return self;
}

#pragma clang diagnostic pop

- (GRPCProtoCall *)RPCToMethod:(NSString *)method
                requestsWriter:(GRXWriter *)requestsWriter
                 responseClass:(Class)responseClass
            responsesWriteable:(id<GRXWriteable>)responsesWriteable {
  GRPCProtoMethod *methodName =
      [[GRPCProtoMethod alloc] initWithPackage:_packageName service:_serviceName method:method];
  return [[GRPCProtoCall alloc] initWithHost:_host
                                      method:methodName
                              requestsWriter:requestsWriter
                               responseClass:responseClass
                          responsesWriteable:responsesWriteable];
}

- (GRPCUnaryProtoCall *)RPCToMethod:(NSString *)method
                            message:(id)message
                    responseHandler:(id<GRPCProtoResponseHandler>)handler
                        callOptions:(GRPCCallOptions *)callOptions
                      responseClass:(Class)responseClass {
  GRPCProtoMethod *methodName =
      [[GRPCProtoMethod alloc] initWithPackage:_packageName service:_serviceName method:method];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:_host
                                          path:methodName.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  return [[GRPCUnaryProtoCall alloc] initWithRequestOptions:requestOptions
                                                    message:message
                                            responseHandler:handler
                                                callOptions:callOptions ?: _callOptions
                                              responseClass:responseClass];
}

- (GRPCStreamingProtoCall *)RPCToMethod:(NSString *)method
                        responseHandler:(id<GRPCProtoResponseHandler>)handler
                            callOptions:(GRPCCallOptions *)callOptions
                          responseClass:(Class)responseClass {
  GRPCProtoMethod *methodName =
      [[GRPCProtoMethod alloc] initWithPackage:_packageName service:_serviceName method:method];
  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:_host
                                          path:methodName.HTTPPath
                                        safety:GRPCCallSafetyDefault];
  return [[GRPCStreamingProtoCall alloc] initWithRequestOptions:requestOptions
                                                responseHandler:handler
                                                    callOptions:callOptions ?: _callOptions
                                                  responseClass:responseClass];
}

@end

@implementation GRPCProtoService

@end
