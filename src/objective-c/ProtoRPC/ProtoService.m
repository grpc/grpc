/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import "ProtoService.h"

#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter.h>

#import "ProtoMethod.h"
#import "ProtoRPC.h"

@implementation ProtoService {
  NSString *_host;
  NSString *_packageName;
  NSString *_serviceName;
}

- (instancetype)init {
  return [self initWithHost:nil packageName:nil serviceName:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                 packageName:(NSString *)packageName
                 serviceName:(NSString *)serviceName {
  if (!host || !serviceName) {
    [NSException raise:NSInvalidArgumentException
                format:@"Neither host nor serviceName can be nil."];
  }
  if ((self = [super init])) {
    _host = [host copy];
    _packageName = [packageName copy];
    _serviceName = [serviceName copy];
  }
  return self;
}

- (ProtoRPC *)RPCToMethod:(NSString *)method
           requestsWriter:(GRXWriter *)requestsWriter
            responseClass:(Class)responseClass
       responsesWriteable:(id<GRXWriteable>)responsesWriteable {
  ProtoMethod *methodName = [[ProtoMethod alloc] initWithPackage:_packageName
                                                         service:_serviceName
                                                          method:method];
  return [[ProtoRPC alloc] initWithHost:_host
                                 method:methodName
                         requestsWriter:requestsWriter
                          responseClass:responseClass
                     responsesWriteable:responsesWriteable];
}
@end

@implementation GRPCProtoService

@end
