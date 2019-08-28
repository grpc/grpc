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

#import <Foundation/Foundation.h>

#import "GRPCInterceptor.h"

@implementation GRPCInterceptorManager {
  id<GRPCInterceptorInterface> _nextInterceptor;
  id<GRPCResponseHandler> _previousInterceptor;
}

- (instancetype)initWithNextInterceptor:(id<GRPCInterceptorInterface>)nextInterceptor {
  if ((self = [super init])) {
    _nextInterceptor = nextInterceptor;
  }

  return self;
}

- (void)setPreviousInterceptor:(id<GRPCResponseHandler>)previousInterceptor {
  _previousInterceptor = previousInterceptor;
}

- (void)shutDown {
  _nextInterceptor = nil;
  _previousInterceptor = nil;
}

- (void)startNextInterceptorWithRequest:(GRPCRequestOptions *)requestOptions
                            callOptions:(GRPCCallOptions *)callOptions {
  if (_nextInterceptor != nil) {
    id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
    dispatch_async(copiedNextInterceptor.requestDispatchQueue, ^{
      [copiedNextInterceptor startWithRequestOptions:requestOptions callOptions:callOptions];
    });
  }
}

- (void)writeNextInterceptorWithData:(id)data {
  if (_nextInterceptor != nil) {
    id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
    dispatch_async(copiedNextInterceptor.requestDispatchQueue, ^{
      [copiedNextInterceptor writeData:data];
    });
  }
}

- (void)finishNextInterceptor {
  if (_nextInterceptor != nil) {
    id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
    dispatch_async(copiedNextInterceptor.requestDispatchQueue, ^{
      [copiedNextInterceptor finish];
    });
  }
}

- (void)cancelNextInterceptor {
  if (_nextInterceptor != nil) {
    id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
    dispatch_async(copiedNextInterceptor.requestDispatchQueue, ^{
      [copiedNextInterceptor cancel];
    });
  }
}

/** Notify the next interceptor in the chain to receive more messages */
- (void)receiveNextInterceptorMessages:(NSUInteger)numberOfMessages {
  if (_nextInterceptor != nil) {
    id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
    dispatch_async(copiedNextInterceptor.requestDispatchQueue, ^{
      [copiedNextInterceptor receiveNextMessages:numberOfMessages];
    });
  }
}

// Methods to forward GRPCResponseHandler callbacks to the previous object

/** Forward initial metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorWithInitialMetadata:(nullable NSDictionary *)initialMetadata {
  if ([_previousInterceptor respondsToSelector:@selector(didReceiveInitialMetadata:)]) {
    id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
    dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
      [copiedPreviousInterceptor didReceiveInitialMetadata:initialMetadata];
    });
  }
}

/** Forward a received message to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorWithData:(id)data {
  if ([_previousInterceptor respondsToSelector:@selector(didReceiveData:)]) {
    id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
    dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
      [copiedPreviousInterceptor didReceiveData:data];
    });
  }
}

/** Forward call close and trailing metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorCloseWithTrailingMetadata:
            (nullable NSDictionary *)trailingMetadata
                                                      error:(nullable NSError *)error {
  if ([_previousInterceptor respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
    id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
    dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
      [copiedPreviousInterceptor didCloseWithTrailingMetadata:trailingMetadata error:error];
    });
  }
}

/** Forward write completion to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorDidWriteData {
  if ([_previousInterceptor respondsToSelector:@selector(didWriteData)]) {
    id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
    dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
      [copiedPreviousInterceptor didWriteData];
    });
  }
}

@end

@implementation GRPCInterceptor {
  GRPCInterceptorManager *_manager;
  dispatch_queue_t _requestDispatchQueue;
  dispatch_queue_t _responseDispatchQueue;
}

- (instancetype)initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                      requestDispatchQueue:(dispatch_queue_t)requestDispatchQueue
                     responseDispatchQueue:(dispatch_queue_t)responseDispatchQueue {
  if ((self = [super init])) {
    _manager = interceptorManager;
    _requestDispatchQueue = requestDispatchQueue;
    _responseDispatchQueue = responseDispatchQueue;
  }

  return self;
}

- (dispatch_queue_t)requestDispatchQueue {
  return _requestDispatchQueue;
}

- (dispatch_queue_t)dispatchQueue {
  return _responseDispatchQueue;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  [_manager startNextInterceptorWithRequest:requestOptions callOptions:callOptions];
}

- (void)writeData:(id)data {
  [_manager writeNextInterceptorWithData:data];
}

- (void)finish {
  [_manager finishNextInterceptor];
}

- (void)cancel {
  [_manager cancelNextInterceptor];
  [_manager
      forwardPreviousInterceptorCloseWithTrailingMetadata:nil
                                                    error:[NSError
                                                              errorWithDomain:kGRPCErrorDomain
                                                                         code:GRPCErrorCodeCancelled
                                                                     userInfo:@{
                                                                       NSLocalizedDescriptionKey :
                                                                           @"Canceled"
                                                                     }]];
  [_manager shutDown];
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  [_manager receiveNextInterceptorMessages:numberOfMessages];
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  [_manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
}

- (void)didReceiveRawMessage:(id)message {
  NSAssert(NO,
           @"The method didReceiveRawMessage is deprecated and cannot be used with interceptor");
  NSLog(@"The method didReceiveRawMessage is deprecated and cannot be used with interceptor");
  abort();
}

- (void)didReceiveData:(id)data {
  [_manager forwardPreviousInterceptorWithData:data];
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  [_manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata error:error];
  [_manager shutDown];
}

- (void)didWriteData {
  [_manager forwardPreviousInterceptorDidWriteData];
}

@end
