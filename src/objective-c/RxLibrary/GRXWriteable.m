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

#import "GRXWriteable.h"

@implementation GRXWriteable {
  GRXValueHandler _valueHandler;
  GRXCompletionHandler _completionHandler;
}

+ (instancetype)writeableWithSingleHandler:(GRXSingleHandler)handler {
  if (!handler) {
    return [[self alloc] init];
  }
  // We nilify this variable when the block is invoked, so that handler is only invoked once even if
  // the writer tries to write multiple values.
  __block GRXEventHandler eventHandler = ^(BOOL done, id value, NSError *error) {
    // Nillify eventHandler before invoking handler, in case the latter causes the former to be
    // executed recursively. Because blocks can be deallocated even during execution, we have to
    // first retain handler locally to guarantee it's valid.
    // TODO(jcanizales): Just turn this craziness into a simple subclass of GRXWriteable.
    GRXSingleHandler singleHandler = handler;
    eventHandler = nil;

    if (value) {
      singleHandler(value, nil);
    } else if (error) {
      singleHandler(nil, error);
    } else {
      NSDictionary *userInfo = @{
        NSLocalizedDescriptionKey: @"The writer finished without producing any value."
      };
      // Even though RxLibrary is independent of gRPC, the domain and code here are, for the moment,
      // set to the values of kGRPCErrorDomain and GRPCErrorCodeInternal. This way, the error formed
      // is the one user of gRPC would expect if the server failed to produce a response.
      //
      // TODO(jcanizales): Figure out a way to keep errors of RxLibrary generic without making users
      // of gRPC take care of two different error domains and error code enums. A possibility is to
      // add error handling to GRXWriters or GRXWriteables, and use them to translate errors between
      // the two domains.
      static NSString *kGRPCErrorDomain = @"io.grpc";
      static NSUInteger kGRPCErrorCodeInternal = 13;
      singleHandler(nil, [NSError errorWithDomain:kGRPCErrorDomain
                                             code:kGRPCErrorCodeInternal
                                         userInfo:userInfo]);
    }
  };
  return [self writeableWithEventHandler:^(BOOL done, id value, NSError *error) {
    if (eventHandler) {
      eventHandler(done, value, error);
    }
  }];
}

+ (instancetype)writeableWithEventHandler:(GRXEventHandler)handler {
  if (!handler) {
    return [[self alloc] init];
  }
  return [[self alloc] initWithValueHandler:^(id value) {
    handler(NO, value, nil);
  } completionHandler:^(NSError *errorOrNil) {
    handler(YES, nil, errorOrNil);
  }];
}

- (instancetype)init {
  return [self initWithValueHandler:nil completionHandler:nil];
}

// Designated initializer
- (instancetype)initWithValueHandler:(GRXValueHandler)valueHandler
                   completionHandler:(GRXCompletionHandler)completionHandler {
  if ((self = [super init])) {
    _valueHandler = valueHandler;
    _completionHandler = completionHandler;
  }
  return self;
}

- (void)writeValue:(id)value {
  if (_valueHandler) {
    _valueHandler(value);
  }
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  if (_completionHandler) {
    _completionHandler(errorOrNil);
  }
}
@end
