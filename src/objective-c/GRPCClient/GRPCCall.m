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

#import "GRPCCall.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#import <RxLibrary/GRXConcurrentWriteable.h>

#import "private/GRPCWrappedCall.h"
#import "private/NSData+GRPC.h"
#import "private/NSDictionary+GRPC.h"
#import "private/NSError+GRPC.h"

NSString * const kGRPCStatusMetadataKey = @"io.grpc.StatusMetadataKey";

@interface GRPCCall () <GRXWriteable>
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

  GRPCWrappedCall *_wrappedCall;
  dispatch_once_t _callAlreadyInvoked;

  // The C gRPC library has less guarantees on the ordering of events than we
  // do. Particularly, in the face of errors, there's no ordering guarantee at
  // all. This wrapper over our actual writeable ensures thread-safety and
  // correct ordering.
  GRXConcurrentWriteable *_responseWriteable;
  GRXWriter *_requestWriter;

  // To create a retain cycle when a call is started, up until it finishes. See
  // |startWithWriteable:| and |finishWithError:|.
  GRPCCall *_self;

  NSMutableDictionary *_requestMetadata;
  NSMutableDictionary *_responseMetadata;
}

@synthesize state = _state;

- (instancetype)init {
  return [self initWithHost:nil path:nil requestsWriter:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path
              requestsWriter:(GRXWriter *)requestWriter {
  if (!host || !path) {
    [NSException raise:NSInvalidArgumentException format:@"Neither host nor path can be nil."];
  }
  if (requestWriter.state != GRXWriterStateNotStarted) {
    [NSException raise:NSInvalidArgumentException
                format:@"The requests writer can't be already started."];
  }
  if ((self = [super init])) {
    _wrappedCall = [[GRPCWrappedCall alloc] initWithHost:host path:path];

    // Serial queue to invoke the non-reentrant methods of the grpc_call object.
    _callQueue = dispatch_queue_create("org.grpc.call", NULL);

    _requestWriter = requestWriter;

    _requestMetadata = [NSMutableDictionary dictionary];
    _responseMetadata = [NSMutableDictionary dictionary];
  }
  return self;
}

#pragma mark Metadata

- (NSMutableDictionary *)requestMetadata {
  return _requestMetadata;
}

- (void)setRequestMetadata:(NSDictionary *)requestMetadata {
  _requestMetadata = [NSMutableDictionary dictionaryWithDictionary:requestMetadata];
}

- (NSDictionary *)responseMetadata {
  return _responseMetadata;
}

#pragma mark Finish

- (void)finishWithError:(NSError *)errorOrNil {
  // If the call isn't retained anywhere else, it can be deallocated now.
  _self = nil;

  // If there were still request messages coming, stop them.
  _requestWriter.state = GRXWriterStateFinished;
  _requestWriter = nil;

  if (errorOrNil) {
    [_responseWriteable cancelWithError:errorOrNil];
  } else {
    [_responseWriteable enqueueSuccessfulCompletion];
  }
}

- (void)cancelCall {
  // Can be called from any thread, any number of times.
  [_wrappedCall cancel];
}

- (void)cancel {
  [self finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                            code:GRPCErrorCodeCancelled
                                        userInfo:nil]];
  [self cancelCall];
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
- (void)startReadWithHandler:(void(^)(grpc_byte_buffer *))handler {
  // TODO(jcanizales): Add error handlers for async failures
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpRecvMessage alloc] initWithHandler:handler]]];
}

// Called initially from the network queue once response headers are received,
// then "recursively" from the responseWriteable queue after each response from the
// server has been written.
// If the call is currently paused, this is a noop. Restarting the call will invoke this
// method.
// TODO(jcanizales): Rename to readResponseIfNotPaused.
- (void)startNextRead {
  if (self.state == GRXWriterStatePaused) {
    return;
  }
  __weak GRPCCall *weakSelf = self;
  __weak GRXConcurrentWriteable *weakWriteable = _responseWriteable;

  dispatch_async(_callQueue, ^{
    [weakSelf startReadWithHandler:^(grpc_byte_buffer *message) {
      if (message == NULL) {
        // No more messages from the server
        return;
      }
      NSData *data = [NSData grpc_dataWithByteBuffer:message];
      grpc_byte_buffer_destroy(message);
      if (!data) {
        // The app doesn't have enough memory to hold the server response. We
        // don't want to throw, because the app shouldn't crash for a behavior
        // that's on the hands of any server to have. Instead we finish and ask
        // the server to cancel.
        //
        // TODO(jcanizales): No canonical code is appropriate for this situation
        // (because it's just a client problem). Use another domain and an
        // appropriately-documented code.
        [weakSelf finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                                      code:GRPCErrorCodeInternal
                                                  userInfo:nil]];
        [weakSelf cancelCall];
        return;
      }
      [weakWriteable enqueueValue:data completionHandler:^{
        [weakSelf startNextRead];
      }];
    }];
  });
}

#pragma mark Send headers

// TODO(jcanizales): Rename to commitHeaders.
- (void)sendHeaders:(NSDictionary *)metadata {
  // TODO(jcanizales): Add error handlers for async failures
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpSendMetadata alloc]
                                            initWithMetadata:metadata ?: @{} handler:nil]]];
}

#pragma mark GRXWriteable implementation

// Only called from the call queue. The error handler will be called from the
// network queue if the write didn't succeed.
- (void)writeMessage:(NSData *)message withErrorHandler:(void (^)())errorHandler {

  __weak GRPCCall *weakSelf = self;
  void(^resumingHandler)(void) = ^{
    // Resume the request writer.
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      strongSelf->_requestWriter.state = GRXWriterStateStarted;
    }
  };
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpSendMessage alloc]
                                            initWithMessage:message
                                            handler:resumingHandler]] errorHandler:errorHandler];
}

- (void)writeValue:(id)value {
  // TODO(jcanizales): Throw/assert if value isn't NSData.

  // Pause the input and only resume it when the C layer notifies us that writes
  // can proceed.
  _requestWriter.state = GRXWriterStatePaused;

  __weak GRPCCall *weakSelf = self;
  dispatch_async(_callQueue, ^{
    [weakSelf writeMessage:value withErrorHandler:^{
      [weakSelf finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                                    code:GRPCErrorCodeInternal
                                                userInfo:nil]];
    }];
  });
}

// Only called from the call queue. The error handler will be called from the
// network queue if the requests stream couldn't be closed successfully.
- (void)finishRequestWithErrorHandler:(void (^)())errorHandler {
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpSendClose alloc] init]]
                            errorHandler:errorHandler];
}

- (void)writesFinishedWithError:(NSError *)errorOrNil {
  _requestWriter = nil;
  if (errorOrNil) {
    [self cancel];
  } else {
    __weak GRPCCall *weakSelf = self;
    dispatch_async(_callQueue, ^{
      [weakSelf finishRequestWithErrorHandler:^{
        [weakSelf finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                                      code:GRPCErrorCodeInternal
                                                  userInfo:nil]];
      }];
    });
  }
}

#pragma mark Invoke

// Both handlers will eventually be called, from the network queue. Writes can start immediately
// after this.
// The first one (metadataHandler), when the response headers are received.
// The second one (completionHandler), whenever the RPC finishes for any reason.
- (void)invokeCallWithMetadataHandler:(void(^)(NSDictionary *))metadataHandler
                    completionHandler:(void(^)(NSError *, NSDictionary *))completionHandler {
  // TODO(jcanizales): Add error handlers for async failures
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpRecvMetadata alloc]
                                            initWithHandler:metadataHandler]]];
  [_wrappedCall startBatchWithOperations:@[[[GRPCOpRecvStatus alloc]
                                            initWithHandler:completionHandler]]];
}

- (void)invokeCall {
  __weak GRPCCall *weakSelf = self;
  [self invokeCallWithMetadataHandler:^(NSDictionary *headers) {
    // Response headers received.
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      [strongSelf->_responseMetadata addEntriesFromDictionary:headers];
      [strongSelf startNextRead];
    }
  } completionHandler:^(NSError *error, NSDictionary *trailers) {
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      [strongSelf->_responseMetadata addEntriesFromDictionary:trailers];

      if (error) {
        NSMutableDictionary *userInfo =
            [NSMutableDictionary dictionaryWithDictionary:error.userInfo];
        userInfo[kGRPCStatusMetadataKey] = strongSelf->_responseMetadata;
        error = [NSError errorWithDomain:error.domain code:error.code userInfo:userInfo];
      }
      [strongSelf finishWithError:error];
    }
  }];
  // Now that the RPC has been initiated, request writes can start.
  [_requestWriter startWithWriteable:self];
}

#pragma mark GRXWriter implementation

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  // Create a retain cycle so that this instance lives until the RPC finishes (or is cancelled).
  // This makes RPCs in which the call isn't externally retained possible (as long as it is started
  // before being autoreleased).
  // Care is taken not to retain self strongly in any of the blocks used in this implementation, so
  // that the life of the instance is determined by this retain cycle.
  _self = self;

  _responseWriteable = [[GRXConcurrentWriteable alloc] initWithWriteable:writeable];
  [self sendHeaders:_requestMetadata];
  [self invokeCall];
}

- (void)setState:(GRXWriterState)newState {
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
@end
