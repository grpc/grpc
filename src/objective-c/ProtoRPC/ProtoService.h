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

#import <Foundation/Foundation.h>

@class GRPCProtoCall;
@protocol GRXWriteable;
@class GRXWriter;

@protocol GRPCProtoServiceInit

- (instancetype)initWithHost:(NSString *)host;

@end

__attribute__((deprecated("Please use GRPCProtoService."))) @interface ProtoService
    : NSObject -
      (instancetype)initWithHost : (NSString *)host packageName
    : (NSString *)packageName serviceName : (NSString *)serviceName NS_DESIGNATED_INITIALIZER;

- (GRPCProtoCall *)RPCToMethod:(NSString *)method
                requestsWriter:(GRXWriter *)requestsWriter
                 responseClass:(Class)responseClass
            responsesWriteable:(id<GRXWriteable>)responsesWriteable;
@end

/**
 * This subclass is empty now. Eventually we'll remove ProtoService class
 * to avoid potential naming conflict
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @interface GRPCProtoService : ProtoService
#pragma clang diagnostic pop

                                  @end
