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
#import "private/GRPCTransport+Private.h"

@interface GRPCInterceptorManager ()<GRPCInterceptorInterface, GRPCResponseHandler>

@end

@implementation GRPCInterceptorManager {
  id<GRPCInterceptorInterface> _nextInterceptor;
  id<GRPCResponseHandler> _previousInterceptor;
  GRPCInterceptor *_thisInterceptor;
  dispatch_queue_t _dispatchQueue;
  NSArray<id<GRPCInterceptorFactory>> *_factories;
  GRPCTransportId _transportId;
  BOOL _shutDown;
}

- (instancetype)initWithFactories:(NSArray<id<GRPCInterceptorFactory>> *)factories
              previousInterceptor:(id<GRPCResponseHandler>)previousInterceptor
                      transportId:(nonnull GRPCTransportId)transportId {
  if ((self = [super init])) {
    if (factories.count == 0) {
      [NSException raise:NSInternalInconsistencyException
                  format:@"Interceptor manager must have factories"];
    }
    _thisInterceptor = [factories[0] createInterceptorWithManager:self];
    if (_thisInterceptor == nil) {
      return nil;
    }
    _previousInterceptor = previousInterceptor;
    _factories = factories;
    // Generate interceptor
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000 || __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
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
    dispatch_set_target_queue(_dispatchQueue, _thisInterceptor.dispatchQueue);
    _transportId = transportId;
  }
  return self;
}

- (void)shutDown {
  dispatch_async(_dispatchQueue, ^{
    self->_nextInterceptor = nil;
    self->_previousInterceptor = nil;
    self->_thisInterceptor = nil;
    self->_shutDown = YES;
  });
}

- (void)createNextInterceptor {
  NSAssert(_nextInterceptor == nil, @"Starting the next interceptor more than once");
  NSAssert(_factories.count > 0, @"Interceptor manager of transport cannot start next interceptor");
  if (_nextInterceptor != nil) {
    NSLog(@"Starting the next interceptor more than once");
    return;
  }
  NSMutableArray<id<GRPCInterceptorFactory>> *interceptorFactories = [NSMutableArray
      arrayWithArray:[_factories subarrayWithRange:NSMakeRange(1, _factories.count - 1)]];
  while (_nextInterceptor == nil) {
    if (interceptorFactories.count == 0) {
      _nextInterceptor =
          [[GRPCTransportManager alloc] initWithTransportId:_transportId previousInterceptor:self];
      break;
    } else {
      _nextInterceptor = [[GRPCInterceptorManager alloc] initWithFactories:interceptorFactories
                                                       previousInterceptor:self
                                                               transportId:_transportId];
      if (_nextInterceptor == nil) {
        [interceptorFactories removeObjectAtIndex:0];
      }
    }
  }
  NSAssert(_nextInterceptor != nil, @"Failed to create interceptor or transport.");
  if (_nextInterceptor == nil) {
    NSLog(@"Failed to create interceptor or transport.");
  }
}

- (void)startNextInterceptorWithRequest:(GRPCRequestOptions *)requestOptions
                            callOptions:(GRPCCallOptions *)callOptions {
  if (_nextInterceptor == nil && !_shutDown) {
    [self createNextInterceptor];
  }
  if (_nextInterceptor == nil) {
    return;
  }
  id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
  dispatch_async(copiedNextInterceptor.dispatchQueue, ^{
    [copiedNextInterceptor startWithRequestOptions:requestOptions callOptions:callOptions];
  });
}

- (void)writeNextInterceptorWithData:(id)data {
  if (_nextInterceptor == nil && !_shutDown) {
    [self createNextInterceptor];
  }
  if (_nextInterceptor == nil) {
    return;
  }
  id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
  dispatch_async(copiedNextInterceptor.dispatchQueue, ^{
    [copiedNextInterceptor writeData:data];
  });
}

- (void)finishNextInterceptor {
  if (_nextInterceptor == nil && !_shutDown) {
    [self createNextInterceptor];
  }
  if (_nextInterceptor == nil) {
    return;
  }
  id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
  dispatch_async(copiedNextInterceptor.dispatchQueue, ^{
    [copiedNextInterceptor finish];
  });
}

- (void)cancelNextInterceptor {
  if (_nextInterceptor == nil && !_shutDown) {
    [self createNextInterceptor];
  }
  if (_nextInterceptor == nil) {
    return;
  }
  id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
  dispatch_async(copiedNextInterceptor.dispatchQueue, ^{
    [copiedNextInterceptor cancel];
  });
}

/** Notify the next interceptor in the chain to receive more messages */
- (void)receiveNextInterceptorMessages:(NSUInteger)numberOfMessages {
  if (_nextInterceptor == nil && !_shutDown) {
    [self createNextInterceptor];
  }
  if (_nextInterceptor == nil) {
    return;
  }
  id<GRPCInterceptorInterface> copiedNextInterceptor = _nextInterceptor;
  dispatch_async(copiedNextInterceptor.dispatchQueue, ^{
    [copiedNextInterceptor receiveNextMessages:numberOfMessages];
  });
}

// Methods to forward GRPCResponseHandler callbacks to the previous object

/** Forward initial metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorWithInitialMetadata:(NSDictionary *)initialMetadata {
  if (_previousInterceptor == nil) {
    return;
  }
  id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
  dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
    [copiedPreviousInterceptor didReceiveInitialMetadata:initialMetadata];
  });
}

/** Forward a received message to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorWithData:(id)data {
  if (_previousInterceptor == nil) {
    return;
  }
  id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
  dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
    [copiedPreviousInterceptor didReceiveData:data];
  });
}

/** Forward call close and trailing metadata to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata
                                                      error:(NSError *)error {
  if (_previousInterceptor == nil) {
    return;
  }
  id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
  dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
    [copiedPreviousInterceptor didCloseWithTrailingMetadata:trailingMetadata error:error];
  });
}

/** Forward write completion to the previous interceptor in the chain */
- (void)forwardPreviousInterceptorDidWriteData {
  if (_previousInterceptor == nil) {
    return;
  }
  id<GRPCResponseHandler> copiedPreviousInterceptor = _previousInterceptor;
  dispatch_async(copiedPreviousInterceptor.dispatchQueue, ^{
    [copiedPreviousInterceptor didWriteData];
  });
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  [_thisInterceptor startWithRequestOptions:requestOptions callOptions:callOptions];
}

- (void)writeData:(id)data {
  [_thisInterceptor writeData:data];
}

- (void)finish {
  [_thisInterceptor finish];
}

- (void)cancel {
  [_thisInterceptor cancel];
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  [_thisInterceptor receiveNextMessages:numberOfMessages];
}

- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata {
  if ([_thisInterceptor respondsToSelector:@selector(didReceiveInitialMetadata:)]) {
    [_thisInterceptor didReceiveInitialMetadata:initialMetadata];
  }
}

- (void)didReceiveData:(id)data {
  if ([_thisInterceptor respondsToSelector:@selector(didReceiveData:)]) {
    [_thisInterceptor didReceiveData:data];
  }
}

- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error {
  if ([_thisInterceptor respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
    [_thisInterceptor didCloseWithTrailingMetadata:trailingMetadata error:error];
  }
}

- (void)didWriteData {
  if ([_thisInterceptor respondsToSelector:@selector(didWriteData)]) {
    [_thisInterceptor didWriteData];
  }
}

@end

@implementation GRPCInterceptor {
  GRPCInterceptorManager *_manager;
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                             dispatchQueue:(dispatch_queue_t)dispatchQueue {
  if ((self = [super init])) {
    _manager = interceptorManager;
    _dispatchQueue = dispatchQueue;
  }

  return self;
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
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
