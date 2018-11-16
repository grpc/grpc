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

#import "ProtoRPC.h"

#if GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS
#import <Protobuf/GPBProtocolBuffers.h>
#else
#import <GPBProtocolBuffers.h>
#endif
#import <GRPCClient/GRPCCall.h>
#import <RxLibrary/GRXWriteable.h>
#import <RxLibrary/GRXWriter+Transformations.h>

/**
 * Generate an NSError object that represents a failure in parsing a proto class.
 */
static NSError *ErrorForBadProto(id proto, Class expectedClass, NSError *parsingError) {
  NSDictionary *info = @{
    NSLocalizedDescriptionKey : @"Unable to parse response from the server",
    NSLocalizedRecoverySuggestionErrorKey :
        @"If this RPC is idempotent, retry "
        @"with exponential backoff. Otherwise, query the server status before "
        @"retrying.",
    NSUnderlyingErrorKey : parsingError,
    @"Expected class" : expectedClass,
    @"Received value" : proto,
  };
  // TODO(jcanizales): Use kGRPCErrorDomain and GRPCErrorCodeInternal when they're public.
  return [NSError errorWithDomain:@"io.grpc" code:13 userInfo:info];
}

@implementation GRPCUnaryProtoCall {
  GRPCStreamingProtoCall *_call;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                               message:(GPBMessage *)message
                       responseHandler:(id<GRPCProtoResponseHandler>)handler
                           callOptions:(GRPCCallOptions *)callOptions
                         responseClass:(Class)responseClass {
  if ((self = [super init])) {
    _call = [[GRPCStreamingProtoCall alloc] initWithRequestOptions:requestOptions
                                                   responseHandler:handler
                                                       callOptions:callOptions
                                                     responseClass:responseClass];
    [_call writeMessage:message];
    [_call finish];
  }
  return self;
}

- (void)cancel {
  [_call cancel];
  _call = nil;
}

@end

@interface GRPCStreamingProtoCall ()<GRPCResponseHandler>

@end

@implementation GRPCStreamingProtoCall {
  GRPCRequestOptions *_requestOptions;
  id<GRPCProtoResponseHandler> _handler;
  GRPCCallOptions *_callOptions;
  Class _responseClass;

  GRPCCall2 *_call;
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCProtoResponseHandler>)handler
                           callOptions:(GRPCCallOptions *)callOptions
                         responseClass:(Class)responseClass {
  if (requestOptions.host.length == 0 || requestOptions.path.length == 0) {
    [NSException raise:NSInvalidArgumentException format:@"Neither host nor path can be nil."];
  }
  if (requestOptions.safety > GRPCCallSafetyCacheableRequest) {
    [NSException raise:NSInvalidArgumentException format:@"Invalid call safety value."];
  }
  if (handler == nil) {
    [NSException raise:NSInvalidArgumentException format:@"Response handler required."];
  }

  if ((self = [super init])) {
    _requestOptions = [requestOptions copy];
    _handler = handler;
    _callOptions = [callOptions copy];
    _responseClass = responseClass;

    // Set queue QoS only when iOS version is 8.0 or above and Xcode version is 9.0 or above
#if __IPHONE_OS_VERSION_MAX_ALLOWED < 110000 || __MAC_OS_X_VERSION_MAX_ALLOWED < 101300
    if (@available(iOS 8.0, macOS 10.10, *)) {
      _dispatchQueue = dispatch_queue_create(
          NULL,
          dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, 0));
    } else {
#else
    {
#endif
      _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
    }
    dispatch_set_target_queue(_dispatchQueue, handler.dispatchQueue);

    [self start];
  }
  return self;
}

- (void)start {
  _call = [[GRPCCall2 alloc] initWithRequestOptions:_requestOptions
                                    responseHandler:self
                                        callOptions:_callOptions];
  [_call start];
}

- (void)cancel {
  GRPCCall2 *call;
  @synchronized(self) {
    call = _call;
    _call = nil;
    if ([_handler respondsToSelector:@selector(closedWithTrailingMetadata:error:)]) {
        dispatch_async(_handler.dispatchQueue, ^{
          id<GRPCProtoResponseHandler> handler = nil;
          @synchronized(self) {
            handler = self->_handler;
            self->_handler = nil;
          }
          [handler closedWithTrailingMetadata:nil
                                        error:[NSError errorWithDomain:kGRPCErrorDomain
                                                                  code:GRPCErrorCodeCancelled
                                                              userInfo:@{
                                                                NSLocalizedDescriptionKey :
                                                                    @"Canceled by app"
                                                              }]];
        });
      }
  }
  [call cancel];
}

- (void)writeMessage:(GPBMessage *)message {
  if (![message isKindOfClass:[GPBMessage class]]) {
    [NSException raise:NSInvalidArgumentException format:@"Data must be a valid protobuf type."];
  }

  GRPCCall2 *call;
  @synchronized(self) {
    call = _call;
  }
  [call writeData:[message data]];
}

- (void)finish {
  GRPCCall2 *call;
  @synchronized(self) {
    call = _call;
    _call = nil;
  }
  [call finish];
}

- (void)receivedInitialMetadata:(NSDictionary *)initialMetadata {
  @synchronized (self) {
    if (initialMetadata != nil && [_handler respondsToSelector:@selector(initialMetadata:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> handler = nil;
        @synchronized (self) {
          handler = self->_handler;
        }
        [handler receivedInitialMetadata:initialMetadata];
      });
    }
  }
}

- (void)receivedRawMessage:(NSData *)message {
  if (message == nil) return;

  NSError *error = nil;
  GPBMessage *parsed = [_responseClass parseFromData:message error:&error];
  @synchronized (self) {
    if (parsed && [_handler respondsToSelector:@selector(receivedProtoMessage:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> handler = nil;
        @synchronized (self) {
          handler = self->_handler;
        }
        [handler receivedProtoMessage:parsed];
      });
    } else if (!parsed && [_handler respondsToSelector:@selector(closedWithTrailingMetadata:error:)]){
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> handler = nil;
        @synchronized (self) {
          handler = self->_handler;
          self->_handler = nil;
        }
        [handler closedWithTrailingMetadata:nil
                                      error:ErrorForBadProto(message, _responseClass, error)];
      });
      [_call cancel];
      _call = nil;
    }
  }
}

- (void)closedWithTrailingMetadata:(NSDictionary *)trailingMetadata
                             error:(NSError *)error {
  @synchronized (self) {
    if ([_handler respondsToSelector:@selector(closedWithTrailingMetadata:error:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> handler = nil;
        @synchronized (self) {
          handler = self->_handler;
          self->_handler = nil;
        }
        [handler closedWithTrailingMetadata:trailingMetadata error:error];
      });
    }
    _call = nil;
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
@implementation ProtoRPC {
#pragma clang diagnostic pop
  id<GRXWriteable> _responseWriteable;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-designated-initializers"
- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
              requestsWriter:(GRXWriter *)requestsWriter {
  [NSException raise:NSInvalidArgumentException
              format:@"Please use ProtoRPC's designated initializer instead."];
  return nil;
}
#pragma clang diagnostic pop

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                      method:(GRPCProtoMethod *)method
              requestsWriter:(GRXWriter *)requestsWriter
               responseClass:(Class)responseClass
          responsesWriteable:(id<GRXWriteable>)responsesWriteable {
  // Because we can't tell the type system to constrain the class, we need to check at runtime:
  if (![responseClass respondsToSelector:@selector(parseFromData:error:)]) {
    [NSException raise:NSInvalidArgumentException
                format:@"A protobuf class to parse the responses must be provided."];
  }
  // A writer that serializes the proto messages to send.
  GRXWriter *bytesWriter = [requestsWriter map:^id(GPBMessage *proto) {
    if (![proto isKindOfClass:[GPBMessage class]]) {
      [NSException raise:NSInvalidArgumentException
                  format:@"Request must be a proto message: %@", proto];
    }
    return [proto data];
  }];
  if ((self = [super initWithHost:host path:method.HTTPPath requestsWriter:bytesWriter])) {
    __weak ProtoRPC *weakSelf = self;

    // A writeable that parses the proto messages received.
    _responseWriteable = [[GRXWriteable alloc] initWithValueHandler:^(NSData *value) {
      // TODO(jcanizales): This is done in the main thread, and needs to happen in another thread.
      NSError *error = nil;
      id parsed = [responseClass parseFromData:value error:&error];
      if (parsed) {
        [responsesWriteable writeValue:parsed];
      } else {
        [weakSelf finishWithError:ErrorForBadProto(value, responseClass, error)];
      }
    }
        completionHandler:^(NSError *errorOrNil) {
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

@implementation GRPCProtoCall

@end
