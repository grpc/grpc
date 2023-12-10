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

#import "GRPCCall.h"

#import "GRPCCall+Interceptor.h"
#import "GRPCCallOptions.h"
#import "GRPCInterceptor.h"

#import "GRPCTransport.h"
#import "private/GRPCTransport+Private.h"

/**
 * The response dispatcher creates its own serial dispatch queue and target the queue to the
 * dispatch queue of a user provided response handler. It removes the requirement of having to use
 * serial dispatch queue in the user provided response handler.
 */
@interface GRPCResponseDispatcher : NSObject <GRPCResponseHandler>

- (nullable instancetype)initWithResponseHandler:(id<GRPCResponseHandler>)responseHandler;

@end

@implementation GRPCResponseDispatcher {
  id<GRPCResponseHandler> _responseHandler;
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithResponseHandler:(id<GRPCResponseHandler>)responseHandler {
  if ((self = [super init])) {
    _responseHandler = responseHandler;
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
    dispatch_set_target_queue(_dispatchQueue, _responseHandler.dispatchQueue);
  }

  return self;
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

- (void)didReceiveInitialMetadata:(nullable NSDictionary *)initialMetadata {
  if ([_responseHandler respondsToSelector:@selector(didReceiveInitialMetadata:)]) {
    [_responseHandler didReceiveInitialMetadata:initialMetadata];
  }
}

- (void)didReceiveData:(id)data {
  // For backwards compatibility with didReceiveRawMessage, if the user provided a response handler
  // that handles didReceiveRawMesssage, we issue to that method instead
  if ([_responseHandler respondsToSelector:@selector(didReceiveRawMessage:)]) {
    [_responseHandler didReceiveRawMessage:data];
  } else if ([_responseHandler respondsToSelector:@selector(didReceiveData:)]) {
    [_responseHandler didReceiveData:data];
  }
}

- (void)didCloseWithTrailingMetadata:(nullable NSDictionary *)trailingMetadata
                               error:(nullable NSError *)error {
  if ([_responseHandler respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
    [_responseHandler didCloseWithTrailingMetadata:trailingMetadata error:error];
  }
}

- (void)didWriteData {
  if ([_responseHandler respondsToSelector:@selector(didWriteData)]) {
    [_responseHandler didWriteData];
  }
}

@end

@implementation GRPCRequestOptions

- (instancetype)initWithHost:(NSString *)host path:(NSString *)path safety:(GRPCCallSafety)safety {
  NSAssert(host.length != 0 && path.length != 0, @"host and path cannot be empty");
  if (host.length == 0 || path.length == 0) {
    return nil;
  }
  if ((self = [super init])) {
    _host = [host copy];
    _path = [path copy];
    _safety = safety;
  }
  return self;
}

- (id)copyWithZone:(NSZone *)zone {
  GRPCRequestOptions *request = [[GRPCRequestOptions alloc] initWithHost:_host
                                                                    path:_path
                                                                  safety:_safety];

  return request;
}

@end

/**
 * This class acts as a wrapper for interceptors
 */
@implementation GRPCCall2 {
  /** The handler of responses. */
  id<GRPCResponseHandler> _responseHandler;

  /**
   * Points to the first interceptor in the interceptor chain.
   */
  id<GRPCInterceptorInterface> _firstInterceptor;

  /**
   * The actual call options being used by this call. It is different from the user-provided
   * call options when the user provided a NULL call options object.
   */
  GRPCCallOptions *_actualCallOptions;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler
                           callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(requestOptions.host.length != 0 && requestOptions.path.length != 0,
           @"Neither host nor path can be nil.");
  NSAssert(responseHandler != nil, @"Response handler required.");
  if (requestOptions.host.length == 0 || requestOptions.path.length == 0) {
    return nil;
  }
  if (responseHandler == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _requestOptions = [requestOptions copy];
    _callOptions = [callOptions copy];
    if (!_callOptions) {
      _actualCallOptions = [[GRPCCallOptions alloc] init];
    } else {
      _actualCallOptions = [callOptions copy];
    }
    _responseHandler = responseHandler;

    GRPCResponseDispatcher *dispatcher =
        [[GRPCResponseDispatcher alloc] initWithResponseHandler:_responseHandler];
    NSMutableArray<id<GRPCInterceptorFactory>> *interceptorFactories;
    if (_actualCallOptions.interceptorFactories != nil) {
      interceptorFactories =
          [NSMutableArray arrayWithArray:_actualCallOptions.interceptorFactories];
    } else {
      interceptorFactories = [NSMutableArray array];
    }
    id<GRPCInterceptorFactory> globalInterceptorFactory = [GRPCCall2 globalInterceptorFactory];
    if (globalInterceptorFactory != nil) {
      [interceptorFactories addObject:globalInterceptorFactory];
    }
    if (_actualCallOptions.transport != NULL) {
      id<GRPCTransportFactory> transportFactory = [[GRPCTransportRegistry sharedInstance]
          getTransportFactoryWithID:_actualCallOptions.transport];

      NSArray<id<GRPCInterceptorFactory>> *transportInterceptorFactories =
          transportFactory.transportInterceptorFactories;
      if (transportInterceptorFactories != nil) {
        [interceptorFactories addObjectsFromArray:transportInterceptorFactories];
      }
    }
    // continuously create interceptor until one is successfully created
    while (_firstInterceptor == nil) {
      if (interceptorFactories.count == 0) {
        _firstInterceptor =
            [[GRPCTransportManager alloc] initWithTransportID:_actualCallOptions.transport
                                          previousInterceptor:dispatcher];
        break;
      } else {
        _firstInterceptor =
            [[GRPCInterceptorManager alloc] initWithFactories:interceptorFactories
                                          previousInterceptor:dispatcher
                                                  transportID:_actualCallOptions.transport];
        if (_firstInterceptor == nil) {
          [interceptorFactories removeObjectAtIndex:0];
        }
      }
    }
    NSAssert(_firstInterceptor != nil, @"Failed to create interceptor or transport.");
    if (_firstInterceptor == nil) {
      NSLog(@"Failed to create interceptor or transport.");
    }
  }
  return self;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler {
  return [self initWithRequestOptions:requestOptions
                      responseHandler:responseHandler
                          callOptions:nil];
}

- (void)start {
  id<GRPCInterceptorInterface> copiedFirstInterceptor = _firstInterceptor;
  GRPCRequestOptions *requestOptions = _requestOptions;
  GRPCCallOptions *callOptions = _actualCallOptions;
  dispatch_async(copiedFirstInterceptor.dispatchQueue, ^{
    [copiedFirstInterceptor startWithRequestOptions:requestOptions callOptions:callOptions];
  });
}

- (void)cancel {
  id<GRPCInterceptorInterface> copiedFirstInterceptor = _firstInterceptor;
  if (copiedFirstInterceptor != nil) {
    dispatch_async(copiedFirstInterceptor.dispatchQueue, ^{
      [copiedFirstInterceptor cancel];
    });
  }
}

- (void)writeData:(id)data {
  id<GRPCInterceptorInterface> copiedFirstInterceptor = _firstInterceptor;
  dispatch_async(copiedFirstInterceptor.dispatchQueue, ^{
    [copiedFirstInterceptor writeData:data];
  });
}

- (void)finish {
  id<GRPCInterceptorInterface> copiedFirstInterceptor = _firstInterceptor;
  dispatch_async(copiedFirstInterceptor.dispatchQueue, ^{
    [copiedFirstInterceptor finish];
  });
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  id<GRPCInterceptorInterface> copiedFirstInterceptor = _firstInterceptor;
  dispatch_async(copiedFirstInterceptor.dispatchQueue, ^{
    [copiedFirstInterceptor receiveNextMessages:numberOfMessages];
  });
}

@end
