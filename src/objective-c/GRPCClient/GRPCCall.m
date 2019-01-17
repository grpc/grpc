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

#import "GRPCCall+OAuth2.h"

#import <RxLibrary/GRXBufferedPipe.h>
#import <RxLibrary/GRXConcurrentWriteable.h>
#import <RxLibrary/GRXImmediateSingleWriter.h>
#import <RxLibrary/GRXWriter+Immediate.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>

#import "GRPCCallOptions.h"
#import "private/GRPCChannelPool.h"
#import "private/GRPCCompletionQueue.h"
#import "private/GRPCConnectivityMonitor.h"
#import "private/GRPCHost.h"
#import "private/GRPCRequestHeaders.h"
#import "private/GRPCWrappedCall.h"
#import "private/NSData+GRPC.h"
#import "private/NSDictionary+GRPC.h"
#import "private/NSError+GRPC.h"

// At most 6 ops can be in an op batch for a client: SEND_INITIAL_METADATA,
// SEND_MESSAGE, SEND_CLOSE_FROM_CLIENT, RECV_INITIAL_METADATA, RECV_MESSAGE,
// and RECV_STATUS_ON_CLIENT.
NSInteger kMaxClientBatch = 6;

NSString *const kGRPCHeadersKey = @"io.grpc.HeadersKey";
NSString *const kGRPCTrailersKey = @"io.grpc.TrailersKey";
static NSMutableDictionary *callFlags;

static NSString *const kAuthorizationHeader = @"authorization";
static NSString *const kBearerPrefix = @"Bearer ";

const char *kCFStreamVarName = "grpc_cfstream";

@interface GRPCCall ()<GRXWriteable>
// Make them read-write.
@property(atomic, strong) NSDictionary *responseHeaders;
@property(atomic, strong) NSDictionary *responseTrailers;

- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
                  callSafety:(GRPCCallSafety)safety
              requestsWriter:(GRXWriter *)requestsWriter
                 callOptions:(GRPCCallOptions *)callOptions;

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
  GRPCRequestOptions *request =
      [[GRPCRequestOptions alloc] initWithHost:_host path:_path safety:_safety];

  return request;
}

@end

@implementation GRPCCall2 {
  /** Options for the call. */
  GRPCCallOptions *_callOptions;
  /** The handler of responses. */
  id<GRPCResponseHandler> _handler;

  // Thread safety of ivars below are protected by _dispatchQueue.

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
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler
                           callOptions:(GRPCCallOptions *)callOptions {
  NSAssert(requestOptions.host.length != 0 && requestOptions.path.length != 0,
           @"Neither host nor path can be nil.");
  NSAssert(requestOptions.safety <= GRPCCallSafetyCacheableRequest, @"Invalid call safety value.");
  NSAssert(responseHandler != nil, @"Response handler required.");
  if (requestOptions.host.length == 0 || requestOptions.path.length == 0) {
    return nil;
  }
  if (requestOptions.safety > GRPCCallSafetyCacheableRequest) {
    return nil;
  }
  if (responseHandler == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _requestOptions = [requestOptions copy];
    if (callOptions == nil) {
      _callOptions = [[GRPCCallOptions alloc] init];
    } else {
      _callOptions = [callOptions copy];
    }
    _handler = responseHandler;
    _initialMetadataPublished = NO;
    _pipe = [GRXBufferedPipe pipe];
    // Set queue QoS only when iOS version is 8.0 or above and Xcode version is 9.0 or above
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
    dispatch_set_target_queue(_dispatchQueue, responseHandler.dispatchQueue);
    _started = NO;
    _canceled = NO;
    _finished = NO;
  }

  return self;
}

- (instancetype)initWithRequestOptions:(GRPCRequestOptions *)requestOptions
                       responseHandler:(id<GRPCResponseHandler>)responseHandler {
  return
      [self initWithRequestOptions:requestOptions responseHandler:responseHandler callOptions:nil];
}

- (void)start {
  GRPCCall *copiedCall = nil;
  @synchronized(self) {
    NSAssert(!_started, @"Call already started.");
    NSAssert(!_canceled, @"Call already canceled.");
    if (_started) {
      return;
    }
    if (_canceled) {
      return;
    }

    _started = YES;
    if (!_callOptions) {
      _callOptions = [[GRPCCallOptions alloc] init];
    }

    _call = [[GRPCCall alloc] initWithHost:_requestOptions.host
                                      path:_requestOptions.path
                                callSafety:_requestOptions.safety
                            requestsWriter:_pipe
                               callOptions:_callOptions];
    if (_callOptions.initialMetadata) {
      [_call.requestHeaders addEntriesFromDictionary:_callOptions.initialMetadata];
    }
    copiedCall = _call;
  }

  void (^valueHandler)(id value) = ^(id value) {
    @synchronized(self) {
      if (self->_handler) {
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
      if (self->_handler) {
        if (!self->_initialMetadataPublished) {
          self->_initialMetadataPublished = YES;
          [self issueInitialMetadata:self->_call.responseHeaders];
        }
        [self issueClosedWithTrailingMetadata:self->_call.responseTrailers error:errorOrNil];
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

    if ([_handler respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
      dispatch_async(_dispatchQueue, ^{
        // Copy to local so that block is freed after cancellation completes.
        id<GRPCResponseHandler> copiedHandler = nil;
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

- (void)writeData:(NSData *)data {
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
  @synchronized(self) {
    if (initialMetadata != nil &&
        [_handler respondsToSelector:@selector(didReceiveInitialMetadata:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
        }
        [copiedHandler didReceiveInitialMetadata:initialMetadata];
      });
    }
  }
}

- (void)issueMessage:(id)message {
  @synchronized(self) {
    if (message != nil && [_handler respondsToSelector:@selector(didReceiveRawMessage:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
        }
        [copiedHandler didReceiveRawMessage:message];
      });
    }
  }
}

- (void)issueClosedWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  @synchronized(self) {
    if ([_handler respondsToSelector:@selector(didCloseWithTrailingMetadata:error:)]) {
      dispatch_async(_dispatchQueue, ^{
        id<GRPCResponseHandler> copiedHandler = nil;
        @synchronized(self) {
          copiedHandler = self->_handler;
          // Clean up _handler so that no more responses are reported to the handler.
          self->_handler = nil;
        }
        [copiedHandler didCloseWithTrailingMetadata:trailingMetadata error:error];
      });
    } else {
      _handler = nil;
    }
  }
}

@end

// The following methods of a C gRPC call object aren't reentrant, and thus
// calls to them must be serialized:
// - start_batch
// - destroy
//
// start_batch with a SEND_MESSAGE argument can only be called after the
// OP_COMPLETE event for any previous write is received. This is achieved by
// pausing the requests writer immediately every time it writes a value, and
// resuming it again when OP_COMPLETE is received.
//
// Similarly, start_batch with a RECV_MESSAGE argument can only be called after
// the OP_COMPLETE event for any previous read is received.This is easier to
// enforce, as we're writing the received messages into the writeable:
// start_batch is enqueued once upon receiving the OP_COMPLETE event for the
// RECV_METADATA batch, and then once after receiving each OP_COMPLETE event for
// each RECV_MESSAGE batch.
@implementation GRPCCall {
  dispatch_queue_t _callQueue;

  NSString *_host;
  NSString *_path;
  GRPCCallSafety _callSafety;
  GRPCCallOptions *_callOptions;
  GRPCWrappedCall *_wrappedCall;
  GRPCConnectivityMonitor *_connectivityMonitor;

  // The C gRPC library has less guarantees on the ordering of events than we
  // do. Particularly, in the face of errors, there's no ordering guarantee at
  // all. This wrapper over our actual writeable ensures thread-safety and
  // correct ordering.
  GRXConcurrentWriteable *_responseWriteable;

  // The network thread wants the requestWriter to resume (when the server is ready for more input),
  // or to stop (on errors), concurrently with user threads that want to start it, pause it or stop
  // it. Because a writer isn't thread-safe, we'll synchronize those operations on it.
  // We don't use a dispatch queue for that purpose, because the writer can call writeValue: or
  // writesFinishedWithError: on this GRPCCall as part of those operations. We want to be able to
  // pause the writer immediately on writeValue:, so we need our locking to be recursive.
  GRXWriter *_requestWriter;

  // To create a retain cycle when a call is started, up until it finishes. See
  // |startWithWriteable:| and |finishWithError:|. This saves users from having to retain a
  // reference to the call object if all they're interested in is the handler being executed when
  // the response arrives.
  GRPCCall *_retainSelf;

  GRPCRequestHeaders *_requestHeaders;

  // In the case that the call is a unary call (i.e. the writer to GRPCCall is of type
  // GRXImmediateSingleWriter), GRPCCall will delay sending ops (not send them to C core
  // immediately) and buffer them into a batch _unaryOpBatch. The batch is sent to C core when
  // the SendClose op is added.
  BOOL _unaryCall;
  NSMutableArray *_unaryOpBatch;

  // The dispatch queue to be used for enqueuing responses to user. Defaulted to the main dispatch
  // queue
  dispatch_queue_t _responseQueue;

  // The OAuth2 token fetched from a token provider.
  NSString *_fetchedOauth2AccessToken;
}

@synthesize state = _state;

+ (void)initialize {
  // Guarantees the code in {} block is invoked only once. See ref at:
  // https://developer.apple.com/documentation/objectivec/nsobject/1418639-initialize?language=objc
  if (self == [GRPCCall self]) {
    grpc_init();
    callFlags = [NSMutableDictionary dictionary];
  }
}

+ (void)setCallSafety:(GRPCCallSafety)callSafety host:(NSString *)host path:(NSString *)path {
  if (host.length == 0 || path.length == 0) {
    return;
  }
  NSString *hostAndPath = [NSString stringWithFormat:@"%@/%@", host, path];
  @synchronized(callFlags) {
    switch (callSafety) {
      case GRPCCallSafetyDefault:
        callFlags[hostAndPath] = @0;
        break;
      case GRPCCallSafetyIdempotentRequest:
        callFlags[hostAndPath] = @GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
        break;
      case GRPCCallSafetyCacheableRequest:
        callFlags[hostAndPath] = @GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
        break;
      default:
        break;
    }
  }
}

+ (uint32_t)callFlagsForHost:(NSString *)host path:(NSString *)path {
  NSString *hostAndPath = [NSString stringWithFormat:@"%@/%@", host, path];
  @synchronized(callFlags) {
    return [callFlags[hostAndPath] intValue];
  }
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
              requestsWriter:(GRXWriter *)requestWriter {
  return [self initWithHost:host
                       path:path
                 callSafety:GRPCCallSafetyDefault
             requestsWriter:requestWriter
                callOptions:nil];
}

- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
                  callSafety:(GRPCCallSafety)safety
              requestsWriter:(GRXWriter *)requestWriter
                 callOptions:(GRPCCallOptions *)callOptions {
  // Purposely using pointer rather than length (host.length == 0) for backwards compatibility.
  NSAssert(host != nil && path != nil, @"Neither host nor path can be nil.");
  NSAssert(safety <= GRPCCallSafetyCacheableRequest, @"Invalid call safety value.");
  NSAssert(requestWriter.state == GRXWriterStateNotStarted,
           @"The requests writer can't be already started.");
  if (!host || !path) {
    return nil;
  }
  if (safety > GRPCCallSafetyCacheableRequest) {
    return nil;
  }
  if (requestWriter.state != GRXWriterStateNotStarted) {
    return nil;
  }

  if ((self = [super init])) {
    _host = [host copy];
    _path = [path copy];
    _callSafety = safety;
    _callOptions = [callOptions copy];

    // Serial queue to invoke the non-reentrant methods of the grpc_call object.
    _callQueue = dispatch_queue_create("io.grpc.call", DISPATCH_QUEUE_SERIAL);

    _requestWriter = requestWriter;

    _requestHeaders = [[GRPCRequestHeaders alloc] initWithCall:self];

    if ([requestWriter isKindOfClass:[GRXImmediateSingleWriter class]]) {
      _unaryCall = YES;
      _unaryOpBatch = [NSMutableArray arrayWithCapacity:kMaxClientBatch];
    }

    _responseQueue = dispatch_get_main_queue();
  }
  return self;
}

- (void)setResponseDispatchQueue:(dispatch_queue_t)queue {
  @synchronized(self) {
    if (_state != GRXWriterStateNotStarted) {
      return;
    }
    _responseQueue = queue;
  }
}

#pragma mark Finish

// This function should support being called within a @synchronized(self) block in another function
// Should not manipulate _requestWriter for deadlock prevention.
- (void)finishWithError:(NSError *)errorOrNil {
  @synchronized(self) {
    if (_state == GRXWriterStateFinished) {
      return;
    }
    _state = GRXWriterStateFinished;

    if (errorOrNil) {
      [_responseWriteable cancelWithError:errorOrNil];
    } else {
      [_responseWriteable enqueueSuccessfulCompletion];
    }

    // If the call isn't retained anywhere else, it can be deallocated now.
    _retainSelf = nil;
  }
}

- (void)cancel {
  @synchronized(self) {
    if (_state == GRXWriterStateFinished) {
      return;
    }
    [self finishWithError:[NSError
                              errorWithDomain:kGRPCErrorDomain
                                         code:GRPCErrorCodeCancelled
                                     userInfo:@{NSLocalizedDescriptionKey : @"Canceled by app"}]];
    [_wrappedCall cancel];
  }
  _requestWriter.state = GRXWriterStateFinished;
}

- (void)dealloc {
  __block GRPCWrappedCall *wrappedCall = _wrappedCall;
  dispatch_async(_callQueue, ^{
    wrappedCall = nil;
  });
}

#pragma mark Read messages

// Only called from the call queue.
// The handler will be called from the network queue.
- (void)startReadWithHandler:(void (^)(grpc_byte_buffer *))handler {
  // TODO(jcanizales): Add error handlers for async failures
  [_wrappedCall startBatchWithOperations:@[ [[GRPCOpRecvMessage alloc] initWithHandler:handler] ]];
}

// Called initially from the network queue once response headers are received,
// then "recursively" from the responseWriteable queue after each response from the
// server has been written.
// If the call is currently paused, this is a noop. Restarting the call will invoke this
// method.
// TODO(jcanizales): Rename to readResponseIfNotPaused.
- (void)startNextRead {
  @synchronized(self) {
    if (_state != GRXWriterStateStarted) {
      return;
    }
  }

  dispatch_async(_callQueue, ^{
    __weak GRPCCall *weakSelf = self;
    [self startReadWithHandler:^(grpc_byte_buffer *message) {
      if (message == NULL) {
        // No more messages from the server
        return;
      }
      __strong GRPCCall *strongSelf = weakSelf;
      if (strongSelf == nil) {
        grpc_byte_buffer_destroy(message);
        return;
      }
      NSData *data = [NSData grpc_dataWithByteBuffer:message];
      grpc_byte_buffer_destroy(message);
      if (!data) {
        // The app doesn't have enough memory to hold the server response. We
        // don't want to throw, because the app shouldn't crash for a behavior
        // that's on the hands of any server to have. Instead we finish and ask
        // the server to cancel.
        @synchronized(strongSelf) {
          [strongSelf
              finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                                  code:GRPCErrorCodeResourceExhausted
                                              userInfo:@{
                                                NSLocalizedDescriptionKey :
                                                    @"Client does not have enough memory to "
                                                    @"hold the server response."
                                              }]];
          [strongSelf->_wrappedCall cancel];
        }
        strongSelf->_requestWriter.state = GRXWriterStateFinished;
      } else {
        @synchronized(strongSelf) {
          [strongSelf->_responseWriteable enqueueValue:data
                                     completionHandler:^{
                                       [strongSelf startNextRead];
                                     }];
        }
      }
    }];
  });
}

#pragma mark Send headers

- (void)sendHeaders {
  // TODO (mxyan): Remove after deprecated methods are removed
  uint32_t callSafetyFlags = 0;
  switch (_callSafety) {
    case GRPCCallSafetyDefault:
      callSafetyFlags = 0;
      break;
    case GRPCCallSafetyIdempotentRequest:
      callSafetyFlags = GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
      break;
    case GRPCCallSafetyCacheableRequest:
      callSafetyFlags = GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
      break;
  }

  NSMutableDictionary *headers = [_requestHeaders mutableCopy];
  NSString *fetchedOauth2AccessToken;
  @synchronized(self) {
    fetchedOauth2AccessToken = _fetchedOauth2AccessToken;
  }
  if (fetchedOauth2AccessToken != nil) {
    headers[@"authorization"] = [kBearerPrefix stringByAppendingString:fetchedOauth2AccessToken];
  } else if (_callOptions.oauth2AccessToken != nil) {
    headers[@"authorization"] =
        [kBearerPrefix stringByAppendingString:_callOptions.oauth2AccessToken];
  }

  // TODO(jcanizales): Add error handlers for async failures
  GRPCOpSendMetadata *op = [[GRPCOpSendMetadata alloc]
      initWithMetadata:headers
                 flags:callSafetyFlags
               handler:nil];  // No clean-up needed after SEND_INITIAL_METADATA
  dispatch_async(_callQueue, ^{
    if (!self->_unaryCall) {
      [self->_wrappedCall startBatchWithOperations:@[ op ]];
    } else {
      [self->_unaryOpBatch addObject:op];
    }
  });
}

#pragma mark GRXWriteable implementation

// Only called from the call queue. The error handler will be called from the
// network queue if the write didn't succeed.
// If the call is a unary call, parameter \a errorHandler will be ignored and
// the error handler of GRPCOpSendClose will be executed in case of error.
- (void)writeMessage:(NSData *)message withErrorHandler:(void (^)(void))errorHandler {
  __weak GRPCCall *weakSelf = self;
  void (^resumingHandler)(void) = ^{
    // Resume the request writer.
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      strongSelf->_requestWriter.state = GRXWriterStateStarted;
    }
  };

  GRPCOpSendMessage *op =
      [[GRPCOpSendMessage alloc] initWithMessage:message handler:resumingHandler];
  if (!_unaryCall) {
    [_wrappedCall startBatchWithOperations:@[ op ] errorHandler:errorHandler];
  } else {
    // Ignored errorHandler since it is the same as the one for GRPCOpSendClose.
    // TODO (mxyan): unify the error handlers of all Ops into a single closure.
    [_unaryOpBatch addObject:op];
  }
}

- (void)writeValue:(id)value {
  NSAssert([value isKindOfClass:[NSData class]], @"value must be of type NSData");

  @synchronized(self) {
    if (_state == GRXWriterStateFinished) {
      return;
    }
  }

  // Pause the input and only resume it when the C layer notifies us that writes
  // can proceed.
  _requestWriter.state = GRXWriterStatePaused;

  dispatch_async(_callQueue, ^{
    // Write error is not processed here. It is handled by op batch of GRPC_OP_RECV_STATUS_ON_CLIENT
    [self writeMessage:value withErrorHandler:nil];
  });
}

// Only called from the call queue. The error handler will be called from the
// network queue if the requests stream couldn't be closed successfully.
- (void)finishRequestWithErrorHandler:(void (^)(void))errorHandler {
  if (!_unaryCall) {
    [_wrappedCall startBatchWithOperations:@[ [[GRPCOpSendClose alloc] init] ]
                              errorHandler:errorHandler];
  } else {
    [_unaryOpBatch addObject:[[GRPCOpSendClose alloc] init]];
    [_wrappedCall startBatchWithOperations:_unaryOpBatch errorHandler:errorHandler];
  }
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  if (errorOrNil) {
    [self cancel];
  } else {
    dispatch_async(_callQueue, ^{
      // EOS error is not processed here. It is handled by op batch of GRPC_OP_RECV_STATUS_ON_CLIENT
      [self finishRequestWithErrorHandler:nil];
    });
  }
}

#pragma mark Invoke

// Both handlers will eventually be called, from the network queue. Writes can start immediately
// after this.
// The first one (headersHandler), when the response headers are received.
// The second one (completionHandler), whenever the RPC finishes for any reason.
- (void)invokeCallWithHeadersHandler:(void (^)(NSDictionary *))headersHandler
                   completionHandler:(void (^)(NSError *, NSDictionary *))completionHandler {
  dispatch_async(_callQueue, ^{
    // TODO(jcanizales): Add error handlers for async failures
    [self->_wrappedCall
        startBatchWithOperations:@[ [[GRPCOpRecvMetadata alloc] initWithHandler:headersHandler] ]];
    [self->_wrappedCall
        startBatchWithOperations:@[ [[GRPCOpRecvStatus alloc] initWithHandler:completionHandler] ]];
  });
}

- (void)invokeCall {
  __weak GRPCCall *weakSelf = self;
  [self invokeCallWithHeadersHandler:^(NSDictionary *headers) {
    // Response headers received.
    __strong GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      strongSelf.responseHeaders = headers;
      [strongSelf startNextRead];
    }
  }
      completionHandler:^(NSError *error, NSDictionary *trailers) {
        __strong GRPCCall *strongSelf = weakSelf;
        if (strongSelf) {
          strongSelf.responseTrailers = trailers;

          if (error) {
            NSMutableDictionary *userInfo = [NSMutableDictionary dictionary];
            if (error.userInfo) {
              [userInfo addEntriesFromDictionary:error.userInfo];
            }
            userInfo[kGRPCTrailersKey] = strongSelf.responseTrailers;
            // Since gRPC core does not guarantee the headers block being called before this block,
            // responseHeaders might be nil.
            userInfo[kGRPCHeadersKey] = strongSelf.responseHeaders;
            error = [NSError errorWithDomain:error.domain code:error.code userInfo:userInfo];
          }
          [strongSelf finishWithError:error];
          strongSelf->_requestWriter.state = GRXWriterStateFinished;
        }
      }];
}

#pragma mark GRXWriter implementation

// Lock acquired inside startWithWriteable:
- (void)startCallWithWriteable:(id<GRXWriteable>)writeable {
  @synchronized(self) {
    if (_state == GRXWriterStateFinished) {
      return;
    }

    _responseWriteable =
        [[GRXConcurrentWriteable alloc] initWithWriteable:writeable dispatchQueue:_responseQueue];

    GRPCPooledChannel *channel =
        [[GRPCChannelPool sharedInstance] channelWithHost:_host callOptions:_callOptions];
    _wrappedCall = [channel wrappedCallWithPath:_path
                                completionQueue:[GRPCCompletionQueue completionQueue]
                                    callOptions:_callOptions];

    if (_wrappedCall == nil) {
      [self finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                                code:GRPCErrorCodeUnavailable
                                            userInfo:@{
                                              NSLocalizedDescriptionKey :
                                                  @"Failed to create call or channel."
                                            }]];
      return;
    }

    [self sendHeaders];
    [self invokeCall];

    // Connectivity monitor is not required for CFStream
    char *enableCFStream = getenv(kCFStreamVarName);
    if (enableCFStream == nil || enableCFStream[0] != '1') {
      [GRPCConnectivityMonitor registerObserver:self selector:@selector(connectivityChanged:)];
    }
  }

  // Now that the RPC has been initiated, request writes can start.
  [_requestWriter startWithWriteable:self];
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  id<GRPCAuthorizationProtocol> tokenProvider = nil;
  @synchronized(self) {
    _state = GRXWriterStateStarted;

    // Create a retain cycle so that this instance lives until the RPC finishes (or is cancelled).
    // This makes RPCs in which the call isn't externally retained possible (as long as it is
    // started before being autoreleased). Care is taken not to retain self strongly in any of the
    // blocks used in this implementation, so that the life of the instance is determined by this
    // retain cycle.
    _retainSelf = self;

    if (_callOptions == nil) {
      GRPCMutableCallOptions *callOptions = [[GRPCHost callOptionsForHost:_host] mutableCopy];
      if (_serverName.length != 0) {
        callOptions.serverAuthority = _serverName;
      }
      if (_timeout > 0) {
        callOptions.timeout = _timeout;
      }
      uint32_t callFlags = [GRPCCall callFlagsForHost:_host path:_path];
      if (callFlags != 0) {
        if (callFlags == GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST) {
          _callSafety = GRPCCallSafetyIdempotentRequest;
        } else if (callFlags == GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) {
          _callSafety = GRPCCallSafetyCacheableRequest;
        }
      }

      id<GRPCAuthorizationProtocol> tokenProvider = self.tokenProvider;
      if (tokenProvider != nil) {
        callOptions.authTokenProvider = tokenProvider;
      }
      _callOptions = callOptions;
    }

    NSAssert(_callOptions.authTokenProvider == nil || _callOptions.oauth2AccessToken == nil,
             @"authTokenProvider and oauth2AccessToken cannot be set at the same time");

    tokenProvider = _callOptions.authTokenProvider;
  }

  if (tokenProvider != nil) {
    __weak typeof(self) weakSelf = self;
    [tokenProvider getTokenWithHandler:^(NSString *token) {
      __strong typeof(self) strongSelf = weakSelf;
      if (strongSelf) {
        BOOL startCall = NO;
        @synchronized(strongSelf) {
          if (strongSelf->_state != GRXWriterStateFinished) {
            startCall = YES;
            if (token) {
              strongSelf->_fetchedOauth2AccessToken = [token copy];
            }
          }
        }
        if (startCall) {
          [strongSelf startCallWithWriteable:writeable];
        }
      }
    }];
  } else {
    [self startCallWithWriteable:writeable];
  }
}

- (void)setState:(GRXWriterState)newState {
  @synchronized(self) {
    // Manual transitions are only allowed from the started or paused states.
    if (_state == GRXWriterStateNotStarted || _state == GRXWriterStateFinished) {
      return;
    }

    switch (newState) {
      case GRXWriterStateFinished:
        _state = newState;
        // Per GRXWriter's contract, setting the state to Finished manually
        // means one doesn't wish the writeable to be messaged anymore.
        [_responseWriteable cancelSilently];
        _responseWriteable = nil;
        return;
      case GRXWriterStatePaused:
        _state = newState;
        return;
      case GRXWriterStateStarted:
        if (_state == GRXWriterStatePaused) {
          _state = newState;
          [self startNextRead];
        }
        return;
      case GRXWriterStateNotStarted:
        return;
    }
  }
}

- (void)connectivityChanged:(NSNotification *)note {
  // Cancel underlying call upon this notification.

  // Retain because connectivity manager only keeps weak reference to GRPCCall.
  __strong GRPCCall *strongSelf = self;
  if (strongSelf) {
    @synchronized(strongSelf) {
      [_wrappedCall cancel];
      [strongSelf
          finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                              code:GRPCErrorCodeUnavailable
                                          userInfo:@{
                                            NSLocalizedDescriptionKey : @"Connectivity lost."
                                          }]];
    }
    strongSelf->_requestWriter.state = GRXWriterStateFinished;
  }
}

@end
