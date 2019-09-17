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

#import "GRPCCallInternal.h"

#import <GRPCClient/GRPCCall.h>
#import <GRPCClient/GRPCInterceptor.h>
#import <RxLibrary/GRXBufferedPipe.h>

#import "../GRPCTransport+Private.h"
#import "GRPCCall+V2API.h"

@implementation GRPCCall2Internal {
  /** Request for the call. */
  GRPCRequestOptions *_requestOptions;
  /** Options for the call. */
  GRPCCallOptions *_callOptions;
  /** The interceptor manager to process responses. */
  GRPCTransportManager *_transportManager;

  /**
   * Make use of legacy GRPCCall to make calls. Nullified when call is finished.
   */
  GRPCCall *_call;
  /** Flags whether initial metadata has been published to response handler. */
  BOOL _initialMetadataPublished;
  /** Streaming call writeable to the underlying call. */
  GRXBufferedPipe *_pipe;
  /** Serial dispatch queue for tasks inside the call. */
  dispatch_queue_t _dispatchQueue;
  /** Flags whether call has started. */
  BOOL _started;
  /** Flags whether call has been canceled. */
  BOOL _canceled;
  /** Flags whether call has been finished. */
  BOOL _finished;
  /** The number of pending messages receiving requests. */
  NSUInteger _pendingReceiveNextMessages;
}

- (instancetype)initWithTransportManager:(GRPCTransportManager *)transportManager {
  dispatch_queue_t dispatchQueue;
  // Set queue QoS only when iOS version is 8.0 or above and Xcode version is 9.0 or above
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000 || __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
  if (@available(iOS 8.0, macOS 10.10, *)) {
    dispatchQueue = dispatch_queue_create(
        NULL, dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, 0));
  } else {
#else
  {
#endif
    dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);
  }
  if ((self = [super init])) {
    _pipe = [GRXBufferedPipe pipe];
    _transportManager = transportManager;
    _dispatchQueue = dispatchQueue;
  }
  return self;
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(requestOptions.host.length != 0 && requestOptions.path.length != 0,
           @"Neither host nor path can be nil.");
  NSAssert(requestOptions.safety <= GRPCCallSafetyCacheableRequest, @"Invalid call safety value.");
  if (requestOptions.host.length == 0 || requestOptions.path.length == 0) {
    NSLog(@"Invalid host and path.");
    return;
  }
  if (requestOptions.safety > GRPCCallSafetyCacheableRequest) {
    NSLog(@"Invalid call safety.");
    return;
  }

  GRPCCall *copiedCall = nil;
  @synchronized(self) {
    _requestOptions = requestOptions;
    if (callOptions == nil) {
      _callOptions = [[GRPCCallOptions alloc] init];
    } else {
      _callOptions = [callOptions copy];
    }

    NSAssert(!_started, @"Call already started.");
    NSAssert(!_canceled, @"Call already canceled.");
    if (_started) {
      return;
    }
    if (_canceled) {
      return;
    }

    _started = YES;

    _call = [[GRPCCall alloc] initWithHost:_requestOptions.host
                                      path:_requestOptions.path
                                callSafety:_requestOptions.safety
                            requestsWriter:_pipe
                               callOptions:_callOptions
                                 writeDone:^{
                                   @synchronized(self) {
                                     if (self->_transportManager) {
                                       [self issueDidWriteData];
                                     }
                                   }
                                 }];
    [_call setResponseDispatchQueue:_dispatchQueue];
    if (_callOptions.initialMetadata) {
      [_call.requestHeaders addEntriesFromDictionary:_callOptions.initialMetadata];
    }
    if (_pendingReceiveNextMessages > 0) {
      [_call receiveNextMessages:_pendingReceiveNextMessages];
      _pendingReceiveNextMessages = 0;
    }
    copiedCall = _call;
  }

  void (^valueHandler)(id value) = ^(id value) {
    @synchronized(self) {
      if (self->_transportManager) {
        if (!self->_initialMetadataPublished) {
          self->_initialMetadataPublished = YES;
          [self issueInitialMetadata:self->_call.responseHeaders];
        }
        if (value) {
          [self issueMessage:value];
        }
      }
    }
  };
  void (^completionHandler)(NSError *errorOrNil) = ^(NSError *errorOrNil) {
    @synchronized(self) {
      if (self->_transportManager) {
        if (!self->_initialMetadataPublished) {
          self->_initialMetadataPublished = YES;
          [self issueInitialMetadata:self->_call.responseHeaders];
        }
        [self issueCloseWithTrailingMetadata:self->_call.responseTrailers error:errorOrNil];
      }
      // Clearing _call must happen *after* dispatching close in order to get trailing
      // metadata from _call.
      if (self->_call) {
        // Clean up the request writers. This should have no effect to _call since its
        // response writeable is already nullified.
        [self->_pipe writesFinishedWithError:nil];
        self->_call = nil;
        self->_pipe = nil;
      }
    }
  };
  id<GRXWriteable> responseWriteable =
      [[GRXWriteable alloc] initWithValueHandler:valueHandler completionHandler:completionHandler];
  [copiedCall startWithWriteable:responseWriteable];
}

- (void)cancel {
  GRPCCall *copiedCall = nil;
  @synchronized(self) {
    if (_canceled) {
      return;
    }

    _canceled = YES;

    copiedCall = _call;
    _call = nil;
    _pipe = nil;

    if (_transportManager != nil) {
      [_transportManager
          forwardPreviousInterceptorCloseWithTrailingMetadata:nil
                                                        error:
                                                            [NSError
                                                                errorWithDomain:kGRPCErrorDomain
                                                                           code:
                                                                               GRPCErrorCodeCancelled
                                                                       userInfo:@{
                                                                         NSLocalizedDescriptionKey :
                                                                             @"Canceled by app"
                                                                       }]];
      [_transportManager shutDown];
    }
  }
  [copiedCall cancel];
}

- (void)writeData:(id)data {
  GRXBufferedPipe *copiedPipe = nil;
  @synchronized(self) {
    NSAssert(!_canceled, @"Call already canceled.");
    NSAssert(!_finished, @"Call is half-closed before sending data.");
    if (_canceled) {
      return;
    }
    if (_finished) {
      return;
    }

    if (_pipe) {
      copiedPipe = _pipe;
    }
  }
  [copiedPipe writeValue:data];
}

- (void)finish {
  GRXBufferedPipe *copiedPipe = nil;
  @synchronized(self) {
    NSAssert(_started, @"Call not started.");
    NSAssert(!_canceled, @"Call already canceled.");
    NSAssert(!_finished, @"Call already half-closed.");
    if (!_started) {
      return;
    }
    if (_canceled) {
      return;
    }
    if (_finished) {
      return;
    }

    if (_pipe) {
      copiedPipe = _pipe;
      _pipe = nil;
    }
    _finished = YES;
  }
  [copiedPipe writesFinishedWithError:nil];
}

- (void)issueInitialMetadata:(NSDictionary *)initialMetadata {
  if (initialMetadata != nil) {
    // cannot directly call callback because this may not be running on manager's dispatch queue
    GRPCTransportManager *copiedManager = _transportManager;
    dispatch_async(copiedManager.dispatchQueue, ^{
      [copiedManager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
    });
  }
}

- (void)issueMessage:(id)message {
  if (message != nil) {
    // cannot directly call callback because this may not be running on manager's dispatch queue
    GRPCTransportManager *copiedManager = _transportManager;
    dispatch_async(copiedManager.dispatchQueue, ^{
      [copiedManager forwardPreviousInterceptorWithData:message];
    });
  }
}

- (void)issueCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  // cannot directly call callback because this may not be running on manager's dispatch queue
  GRPCTransportManager *copiedManager = _transportManager;
  dispatch_async(copiedManager.dispatchQueue, ^{
    [copiedManager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata
                                                                 error:error];
    [copiedManager shutDown];
  });
}

- (void)issueDidWriteData {
  // cannot directly call callback because this may not be running on manager's dispatch queue
  GRPCTransportManager *copiedManager = _transportManager;
  dispatch_async(copiedManager.dispatchQueue, ^{
    [copiedManager forwardPreviousInterceptorDidWriteData];
  });
}

- (void)receiveNextMessages:(NSUInteger)numberOfMessages {
  // branching based on _callOptions.flowControlEnabled is handled inside _call
  GRPCCall *copiedCall = nil;
  @synchronized(self) {
    copiedCall = _call;
    if (copiedCall == nil) {
      _pendingReceiveNextMessages += numberOfMessages;
      return;
    }
  }
  [copiedCall receiveNextMessages:numberOfMessages];
}

@end
