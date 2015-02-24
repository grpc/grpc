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

#include <grpc.h>
#include <support/time.h>

#import "GRPCMethodName.h"
#import "private/GRPCChannel.h"
#import "private/GRPCCompletionQueue.h"
#import "private/GRPCDelegateWrapper.h"
#import "private/GRPCMethodName+HTTP2Encoding.h"
#import "private/NSData+GRPC.h"
#import "private/NSDictionary+GRPC.h"
#import "private/NSError+GRPC.h"

// A grpc_call_error represents a precondition failure when invoking the
// grpc_call_* functions. If one ever happens, it's a bug in this library.
//
// TODO(jcanizales): Can an application shut down gracefully when a thread other
// than the main one throws an exception?
static void AssertNoErrorInCall(grpc_call_error error) {
  if (error != GRPC_CALL_OK) {
    @throw [NSException exceptionWithName:NSInternalInconsistencyException
                                   reason:@"Precondition of grpc_call_* not met."
                                 userInfo:nil];
  }
}

@interface GRPCCall () <GRXWriteable>
// Makes it readwrite.
@property(atomic, strong) NSDictionary *responseMetadata;
@end

// The following methods of a C gRPC call object aren't reentrant, and thus
// calls to them must be serialized:
// - add_metadata
// - invoke
// - start_write
// - writes_done
// - start_read
// - destroy
// The first four are called as part of responding to client commands, but
// start_read we want to call as soon as we're notified that the RPC was
// successfully established (which happens concurrently in the network queue).
// Serialization is achieved by using a private serial queue to operate the
// call object.
// Because add_metadata and invoke are called and return successfully before
// any of the other methods is called, they don't need to use the queue.
//
// Furthermore, start_write and writes_done can only be called after the
// WRITE_ACCEPTED event for any previous write is received. This is achieved by
// pausing the requests writer immediately every time it writes a value, and
// resuming it again when WRITE_ACCEPTED is received.
//
// Similarly, start_read can only be called after the READ event for any
// previous read is received. This is easier to enforce, as we're writing the
// received messages into the writeable: start_read is enqueued once upon receiving
// the CLIENT_METADATA_READ event, and then once after receiving each READ
// event.
@implementation GRPCCall {
  dispatch_queue_t _callQueue;

  grpc_call *_gRPCCall;
  dispatch_once_t _callAlreadyInvoked;

  GRPCChannel *_channel;
  GRPCCompletionQueue *_completionQueue;

  // The C gRPC library has less guarantees on the ordering of events than we
  // do. Particularly, in the face of errors, there's no ordering guarantee at
  // all. This wrapper over our actual writeable ensures thread-safety and
  // correct ordering.
  GRPCDelegateWrapper *_responseWriteable;
  id<GRXWriter> _requestWriter;
}

@synthesize state = _state;

- (instancetype)init {
  return [self initWithHost:nil method:nil requestsWriter:nil];
}

// Designated initializer
- (instancetype)initWithHost:(NSString *)host
                      method:(GRPCMethodName *)method
              requestsWriter:(id<GRXWriter>)requestWriter {
  if (!host || !method) {
    [NSException raise:NSInvalidArgumentException format:@"Neither host nor method can be nil."];
  }
  // TODO(jcanizales): Throw if the requestWriter was already started.
  if ((self = [super init])) {
    static dispatch_once_t initialization;
    dispatch_once(&initialization, ^{
      grpc_init();
    });

    _completionQueue = [GRPCCompletionQueue completionQueue];

    _channel = [GRPCChannel channelToHost:host];
    _gRPCCall = grpc_channel_create_call_old(_channel.unmanagedChannel,
                                             method.HTTP2Path.UTF8String,
                                             host.UTF8String,
                                             gpr_inf_future);

    // Serial queue to invoke the non-reentrant methods of the grpc_call object.
    _callQueue = dispatch_queue_create("org.grpc.call", NULL);

    _requestWriter = requestWriter;
  }
  return self;
}

#pragma mark Finish

- (void)finishWithError:(NSError *)errorOrNil {
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
  AssertNoErrorInCall(grpc_call_cancel(_gRPCCall));
}

- (void)cancel {
  [self finishWithError:[NSError errorWithDomain:kGRPCErrorDomain
                                            code:GRPCErrorCodeCancelled
                                        userInfo:nil]];
  [self cancelCall];
}

- (void)dealloc {
  grpc_call *gRPCCall = _gRPCCall;
  dispatch_async(_callQueue, ^{
    grpc_call_destroy(gRPCCall);
  });
}

#pragma mark Read messages

// Only called from the call queue.
// The handler will be called from the network queue.
- (void)startReadWithHandler:(GRPCEventHandler)handler {
  AssertNoErrorInCall(grpc_call_start_read_old(_gRPCCall, (__bridge_retained void *)handler));
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
  __weak GRPCDelegateWrapper *weakWriteable = _responseWriteable;

  dispatch_async(_callQueue, ^{
    [weakSelf startReadWithHandler:^(grpc_event *event) {
      if (!event->data.read) {
        // No more responses from the server.
        return;
      }
      NSData *data = [NSData grpc_dataWithByteBuffer:event->data.read];
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
      [weakWriteable enqueueMessage:data completionHandler:^{
        [weakSelf startNextRead];
      }];
    }];
  });
}

#pragma mark Send headers

- (void)addHeaderWithName:(NSString *)name binaryValue:(NSData *)value {
  grpc_metadata metadata;
  // Safe to discard const qualifiers; we're not going to modify the contents.
  metadata.key = (char *)name.UTF8String;
  metadata.value = (char *)value.bytes;
  metadata.value_length = value.length;
  grpc_call_add_metadata_old(_gRPCCall, &metadata, 0);
}

- (void)addHeaderWithName:(NSString *)name ASCIIValue:(NSString *)value {
  grpc_metadata metadata;
  // Safe to discard const qualifiers; we're not going to modify the contents.
  metadata.key = (char *)name.UTF8String;
  metadata.value = (char *)value.UTF8String;
  // The trailing \0 isn't encoded in HTTP2.
  metadata.value_length = value.length;
  grpc_call_add_metadata_old(_gRPCCall, &metadata, 0);
}

// TODO(jcanizales): Rename to commitHeaders.
- (void)sendHeaders:(NSDictionary *)metadata {
  for (NSString *name in metadata) {
    id value = metadata[name];
    if ([value isKindOfClass:[NSData class]]) {
      [self addHeaderWithName:name binaryValue:value];
    } else if ([value isKindOfClass:[NSString class]]) {
      [self addHeaderWithName:name ASCIIValue:value];
    }
  }
}

#pragma mark GRXWriteable implementation

// Only called from the call queue. The error handler will be called from the
// network queue if the write didn't succeed.
- (void)writeMessage:(NSData *)message withErrorHandler:(void (^)())errorHandler {

  __weak GRPCCall *weakSelf = self;
  GRPCEventHandler resumingHandler = ^(grpc_event *event) {
    if (event->data.write_accepted != GRPC_OP_OK) {
      errorHandler();
    }
    // Resume the request writer (even in the case of error).
    // TODO(jcanizales): No need to do it in the case of errors anymore?
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      strongSelf->_requestWriter.state = GRXWriterStateStarted;
    }
  };

  grpc_byte_buffer *buffer = message.grpc_byteBuffer;
  AssertNoErrorInCall(grpc_call_start_write_old(_gRPCCall,
                                                buffer,
                                                (__bridge_retained void *)resumingHandler,
                                                0));
  grpc_byte_buffer_destroy(buffer);
}

- (void)didReceiveValue:(id)value {
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
  GRPCEventHandler handler = ^(grpc_event *event) {
    if (event->data.finish_accepted != GRPC_OP_OK) {
      errorHandler();
    }
  };
  AssertNoErrorInCall(grpc_call_writes_done_old(_gRPCCall, (__bridge_retained void *)handler));
}

- (void)didFinishWithError:(NSError *)errorOrNil {
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
- (void)invokeCallWithMetadataHandler:(GRPCEventHandler)metadataHandler
                    completionHandler:(GRPCEventHandler)completionHandler {
  AssertNoErrorInCall(grpc_call_invoke_old(_gRPCCall,
                                           _completionQueue.unmanagedQueue,
                                           (__bridge_retained void *)metadataHandler,
                                           (__bridge_retained void *)completionHandler,
                                           0));
}

- (void)invokeCall {
  __weak GRPCCall *weakSelf = self;
  [self invokeCallWithMetadataHandler:^(grpc_event *event) {
    // Response metadata received.
    // TODO(jcanizales): Name the type of event->data.client_metadata_read
    // in the C library so one can actually pass the object to a method.
    grpc_metadata *entries = event->data.client_metadata_read.elements;
    size_t count = event->data.client_metadata_read.count;
    GRPCCall *strongSelf = weakSelf;
    if (strongSelf) {
      strongSelf.responseMetadata = [NSDictionary grpc_dictionaryFromMetadata:entries
                                                                        count:count];
      [strongSelf startNextRead];
    }
  } completionHandler:^(grpc_event *event) {
    // TODO(jcanizales): Merge HTTP2 trailers into response metadata.
    [weakSelf finishWithError:[NSError grpc_errorFromStatus:&event->data.finished]];
  }];
  // Now that the RPC has been initiated, request writes can start.
  [_requestWriter startWithWriteable:self];
}

#pragma mark GRXWriter implementation

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  // The following produces a retain cycle self:_responseWriteable:self, which is only
  // broken when didFinishWithError: is sent to the wrapped writeable.
  // Care is taken not to retain self strongly in any of the blocks used in
  // the implementation of GRPCCall, so that the life of the instance is
  // determined by this retain cycle.
  _responseWriteable = [[GRPCDelegateWrapper alloc] initWithWriteable:writeable writer:self];
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
