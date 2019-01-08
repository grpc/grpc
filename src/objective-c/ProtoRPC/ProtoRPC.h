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
#import <GRPCClient/GRPCCall.h>

#import "ProtoMethod.h"

__attribute__((deprecated("Please use GRPCProtoCall."))) @interface ProtoRPC
    : GRPCCall

      /**
       * host parameter should not contain the scheme (http:// or https://), only the name or IP
       * addr and the port number, for example @"localhost:5050".
       */
      -
      (instancetype)initWithHost : (NSString *)host method
    : (GRPCProtoMethod *)method requestsWriter : (GRXWriter *)requestsWriter responseClass
    : (Class)responseClass responsesWriteable
    : (id<GRXWriteable>)responsesWriteable NS_DESIGNATED_INITIALIZER;

- (void)start;
@end

/**
 * This subclass is empty now. Eventually we'll remove ProtoRPC class
 * to avoid potential naming conflict
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @interface GRPCProtoCall : ProtoRPC
#pragma clang diagnostic pop

                               @end
