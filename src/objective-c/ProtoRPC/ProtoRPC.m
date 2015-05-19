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

#import "ProtoRPC.h"

#import <gRPC/GRXWriteable.h>
#import <gRPC/GRXWriter.h>
#import <gRPC/GRXWriter+Transformations.h>
#import <Protobuf/GPBProtocolBuffers.h>

@implementation ProtoRPC {
  id<GRXWriteable> _responseWriteable;
}

- (instancetype)initWithHost:(NSString *)host
                      method:(GRPCMethodName *)method
              requestsWriter:(id<GRXWriter>)requestsWriter {
  return [self initWithHost:host
                     method:method
             requestsWriter:requestsWriter
              responseClass:nil
        responsesWriteable:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                      method:(GRPCMethodName *)method
              requestsWriter:(id<GRXWriter>)requestsWriter
               responseClass:(Class)responseClass
          responsesWriteable:(id<GRXWriteable>)responsesWriteable {
  // Because we can't tell the type system to constrain the class, we need to check at runtime:
  if (![responseClass respondsToSelector:@selector(parseFromData:)]) {
    [NSException raise:NSInvalidArgumentException
                format:@"A protobuf class to parse the responses must be provided."];
  }
  // A writer that serializes the proto messages to send.
  id<GRXWriter> bytesWriter =
      [[[GRXWriter alloc] initWithWriter:requestsWriter] map:^id(GPBMessage *proto) {
        return [proto data];
      }];
  if ((self = [super initWithHost:host method:method requestsWriter:bytesWriter])) {
    // A writeable that parses the proto messages received.
    _responseWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
      [responsesWriteable writeValue:[responseClass parseFromData:value]];
    } completionHandler:^(NSError *errorOrNil) {
      [responsesWriteable writesFinishedWithError:errorOrNil];
    }];
  }
  return self;
}

- (void)start {
  [self startWithWriteable:_responseWriteable];
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  [super startWithWriteable:writeable];
  // Break retain cycles.
  _responseWriteable = nil;
}
@end
