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
      NSDictionary *userInfo =
          @{NSLocalizedDescriptionKey : @"The writer finished without producing any value."};
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
  return [[self alloc]
      initWithValueHandler:^(id value) {
        handler(NO, value, nil);
      }
      completionHandler:^(NSError *errorOrNil) {
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
