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

#import "ProtoServiceLegacy.h"
#import "ProtoMethod.h"
#import "ProtoRPCLegacy.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
@implementation ProtoService (Legacy)
#pragma clang diagnostic pop

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
  }
  return self;
}

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

@end
