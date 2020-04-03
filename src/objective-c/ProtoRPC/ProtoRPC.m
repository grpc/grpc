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

@implementation GRPCUnaryResponseHandler {
  void (^_responseHandler)(id, NSError *);
  dispatch_queue_t _responseDispatchQueue;

  GPBMessage *_message;
}

- (nullable instancetype)initWithResponseHandler:(void (^)(id, NSError *))handler
                           responseDispatchQueue:(dispatch_queue_t)dispatchQueue {
  if ((self = [super init])) {
    _responseHandler = handler;
    if (dispatchQueue == nil) {
      _responseDispatchQueue = dispatch_get_main_queue();
    } else {
      _responseDispatchQueue = dispatchQueue;
    }
  }
  return self;
}

// Implements GRPCProtoResponseHandler
- (dispatch_queue_t)dispatchQueue {
  return _responseDispatchQueue;
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  _responseHeaders = [initialMetadata copy];
}

- (void)didReceiveProtoMessage:(GPBMessage *)message {
  _message = message;
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  _responseTrailers = [trailingMetadata copy];
  GPBMessage *message = _message;
  _message = nil;
  _responseHandler(message, error);
}

// Intentional no-op since flow control is N/A in a unary call
- (void)didWriteMessage {
}

@end

@implementation GRPCUnaryProtoCall {
  GRPCStreamingProtoCall *_call;
  GPBMessage *_message;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                               message:(GPBMessage *)message
                       responseHandler:(id<GRPCProtoResponseHandler>)handler
                           callOptions:(GRPCCallOptions *)callOptions
                         responseClass:(Class)responseClass {
  NSAssert(message != nil, @"message cannot be empty.");
  NSAssert(responseClass != nil, @"responseClass cannot be empty.");
  if (message == nil || responseClass == nil) {
    return nil;
  }
  if ((self = [super init])) {
    _call = [[GRPCStreamingProtoCall alloc] initWithRequestOptions:requestOptions
                                                   responseHandler:handler
                                                       callOptions:callOptions
                                                     responseClass:responseClass];
    _message = [message copy];
  }
  return self;
}

- (void)start {
  [_call start];
  [_call receiveNextMessage];
  [_call writeMessage:_message];
  [_call finish];
}

- (void)cancel {
  [_call cancel];
}

@end

@interface GRPCStreamingProtoCall () <GRPCResponseHandler>

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
  NSAssert(requestOptions.host.length != 0 && requestOptions.path.length != 0 &&
               requestOptions.safety <= GRPCCallSafetyCacheableRequest,
           @"Invalid callOptions.");
  NSAssert(handler != nil, @"handler cannot be empty.");
  if (requestOptions.host.length == 0 || requestOptions.path.length == 0 ||
      requestOptions.safety > GRPCCallSafetyCacheableRequest) {
    return nil;
  }
  if (handler == nil) {
    return nil;
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
      _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
    }
    dispatch_set_target_queue(_dispatchQueue, handler.dispatchQueue);

    _call = [[GRPCCall2 alloc] initWithRequestOptions:_requestOptions
                                      responseHandler:self
                                          callOptions:_callOptions];
  }
  return self;
}

- (void)start {
  GRPCCall2 *copiedCall;
  @synchronized(self) {
    copiedCall = _call;
  }
  [copiedCall start];
}

- (void)cancel {
  GRPCCall2 *copiedCall;
  @synchronized(self) {
    copiedCall = _call;
    _call = nil;
    if ([_handler respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
          self->_handler = nil;
        }
        [copiedHandler didCloseWithTrailingMetadata:nil
                                              error:[NSError errorWithDomain:kGRPCErrorDomain
                                                                        code:GRPCErrorCodeCancelled
                                                                    userInfo:@{
                                                                      NSLocalizedDescriptionKey :
                                                                          @"Canceled by app"
                                                                    }]];
      });
    } else {
      _handler = nil;
    }
  }
  [copiedCall cancel];
}

- (void)writeMessage:(GPBMessage *)message {
  NSAssert([message isKindOfClass:[GPBMessage class]], @"Parameter message must be a GPBMessage");
  if (![message isKindOfClass:[GPBMessage class]]) {
    NSLog(@"Failed to send a message that is non-proto.");
    return;
  }

  GRPCCall2 *copiedCall;
  @synchronized(self) {
    copiedCall = _call;
  }
  [copiedCall writeData:[message data]];
}

- (void)finish {
  GRPCCall2 *copiedCall;
  @synchronized(self) {
    copiedCall = _call;
    _call = nil;
  }
  [copiedCall finish];
}

- (void)receiveNextMessage {
  [self receiveNextMessages:1];
}
- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  GRPCCall2 *copiedCall;
  @synchronized(self) {
    copiedCall = _call;
  }
  [copiedCall receiveNextMessages:numberOfMessages];
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  @synchronized(self) {
    if (initialMetadata != nil &&
        [_handler respondsToSelector:@selector(didReceiveInitialMetadata:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
        }
        [copiedHandler didReceiveInitialMetadata:initialMetadata];
      });
    }
  }
}

- (void)didReceiveData:(id)data {
  if (data == nil) return;

  NSError *error = nil;
  GPBMessage *parsed = [_responseClass parseFromData:data error:&error];
  @synchronized(self) {
    if (parsed && [_handler respondsToSelector:@selector(didReceiveProtoMessage:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
        }
        [copiedHandler didReceiveProtoMessage:parsed];
      });
    } else if (!parsed && [_handler respondsToSelector:@selector(didCloseWithTrailingMetadata:
                                                                                        error:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
          self->_handler = nil;
        }
        [copiedHandler
            didCloseWithTrailingMetadata:nil
                                   error:ErrorForBadProto(data, self->_responseClass, error)];
      });
      [_call cancel];
      _call = nil;
    }
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  @synchronized(self) {
    if ([_handler respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
          self->_handler = nil;
        }
        [copiedHandler didCloseWithTrailingMetadata:trailingMetadata error:error];
      });
    }
    _call = nil;
  }
}

- (void)didWriteData {
  @synchronized(self) {
    if ([_handler respondsToSelector:@selector(didWriteMessage)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCProtoResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
        }
        [copiedHandler didWriteMessage];
      });
    }
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end

/**
 * Generate an NSError object that represents a failure in parsing a proto class.
 */
NSError *ErrorForBadProto(id proto, Class expectedClass, NSError *parsingError) {
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
