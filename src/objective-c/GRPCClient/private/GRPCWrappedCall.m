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

#import "GRPCWrappedCall.h"

#import <Foundation/Foundation.h>
#include <grpc/grpc.h>
#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>

#import "GRPCCompletionQueue.h"
#import "GRPCHost.h"
#import "NSDictionary+GRPC.h"
#import "NSData+GRPC.h"
#import "NSError+GRPC.h"

@implementation GRPCOperation {
@protected
  // Most operation subclasses don't set any flags in the grpc_op, and rely on the flag member being
  // initialized to zero.
  grpc_op _op;
  void(^_handler)();
}

- (void)finish {
  if (_handler) {
    void(^handler)() = _handler;
    _handler = nil;
    handler();
  }
}
@end

@implementation GRPCOpSendMetadata

- (instancetype)init {
  return [self initWithMetadata:nil handler:nil];
}

- (instancetype)initWithMetadata:(NSDictionary *)metadata handler:(void (^)())handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_SEND_INITIAL_METADATA;
    _op.data.send_initial_metadata.count = metadata.count;
    _op.data.send_initial_metadata.metadata = metadata.grpc_metadataArray;
    _op.data.send_initial_metadata.maybe_compression_level.is_set = false;
    _op.data.send_initial_metadata.maybe_compression_level.level = 0;
    _handler = handler;
  }
  return self;
}

- (void)dealloc {
  gpr_free(_op.data.send_initial_metadata.metadata);
}

@end

@implementation GRPCOpSendMessage

- (instancetype)init {
  return [self initWithMessage:nil handler:nil];
}

- (instancetype)initWithMessage:(NSData *)message handler:(void (^)())handler {
  if (!message) {
    [NSException raise:NSInvalidArgumentException format:@"message cannot be nil"];
  }
  if (self = [super init]) {
    _op.op = GRPC_OP_SEND_MESSAGE;
    _op.data.send_message = message.grpc_byteBuffer;
    _handler = handler;
  }
  return self;
}

- (void)dealloc {
  gpr_free(_op.data.send_message);
}

@end

@implementation GRPCOpSendClose

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)())handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    _handler = handler;
  }
  return self;
}

@end

@implementation GRPCOpRecvMetadata {
  grpc_metadata_array _headers;
}

- (instancetype) init {
  return [self initWithHandler:nil];
}

- (instancetype) initWithHandler:(void (^)(NSDictionary *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_INITIAL_METADATA;
    grpc_metadata_array_init(&_headers);
    _op.data.recv_initial_metadata = &_headers;
    if (handler) {
      // Prevent reference cycle with _handler
      __weak typeof(self) weakSelf = self;
      _handler = ^{
        __strong typeof(self) strongSelf = weakSelf;
        NSDictionary *metadata = [NSDictionary
                                  grpc_dictionaryFromMetadataArray:strongSelf->_headers];
        handler(metadata);
      };
    }
  }
  return self;
}

- (void)dealloc {
  grpc_metadata_array_destroy(&_headers);
}

@end

@implementation GRPCOpRecvMessage{
  grpc_byte_buffer *_receivedMessage;
}

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)(grpc_byte_buffer *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_MESSAGE;
    _op.data.recv_message = &_receivedMessage;
    if (handler) {
      // Prevent reference cycle with _handler
      __weak typeof(self) weakSelf = self;
      _handler = ^{
        __strong typeof(self) strongSelf = weakSelf;
        handler(strongSelf->_receivedMessage);
      };
    }
  }
  return self;
}

@end

@implementation GRPCOpRecvStatus{
  grpc_status_code _statusCode;
  char *_details;
  size_t _detailsCapacity;
  grpc_metadata_array _trailers;
}

- (instancetype) init {
  return [self initWithHandler:nil];
}

- (instancetype) initWithHandler:(void (^)(NSError *, NSDictionary *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    _op.data.recv_status_on_client.status = &_statusCode;
    _op.data.recv_status_on_client.status_details = &_details;
    _op.data.recv_status_on_client.status_details_capacity = &_detailsCapacity;
    grpc_metadata_array_init(&_trailers);
    _op.data.recv_status_on_client.trailing_metadata = &_trailers;
    if (handler) {
      // Prevent reference cycle with _handler
      __weak typeof(self) weakSelf = self;
      _handler = ^{
        __strong typeof(self) strongSelf = weakSelf;
        NSError *error = [NSError grpc_errorFromStatusCode:strongSelf->_statusCode
                                                   details:strongSelf->_details];
        NSDictionary *trailers = [NSDictionary
                                  grpc_dictionaryFromMetadataArray:strongSelf->_trailers];
        handler(error, trailers);
      };
    }
  }
  return self;
}

- (void)dealloc {
  grpc_metadata_array_destroy(&_trailers);
  gpr_free(_details);
}

@end

#pragma mark GRPCWrappedCall

@implementation GRPCWrappedCall {
  GRPCCompletionQueue *_queue;
  grpc_call *_call;
}

- (instancetype)init {
  return [self initWithHost:nil path:nil];
}

- (instancetype)initWithHost:(NSString *)host
                        path:(NSString *)path {
  if (!path || !host) {
    [NSException raise:NSInvalidArgumentException
                format:@"path and host cannot be nil."];
  }

  if (self = [super init]) {
    // Each completion queue consumes one thread. There's a trade to be made between creating and
    // consuming too many threads and having contention of multiple calls in a single completion
    // queue. Currently we use a singleton queue.
    _queue = [GRPCCompletionQueue completionQueue];

    _call = [[GRPCHost hostWithAddress:host] unmanagedCallWithPath:path completionQueue:_queue];
    if (_call == NULL) {
      return nil;
    }
  }
  return self;
}

- (void)startBatchWithOperations:(NSArray *)operations {
  [self startBatchWithOperations:operations errorHandler:nil];
}

- (void)startBatchWithOperations:(NSArray *)operations errorHandler:(void (^)())errorHandler {
  size_t nops = operations.count;
  grpc_op *ops_array = gpr_malloc(nops * sizeof(grpc_op));
  size_t i = 0;
  for (GRPCOperation *operation in operations) {
    ops_array[i++] = operation.op;
  }
  grpc_call_error error = grpc_call_start_batch(_call, ops_array, nops,
                                                (__bridge_retained void *)(^(bool success){
    if (!success) {
      if (errorHandler) {
        errorHandler();
      } else {
        return;
      }
    }
    for (GRPCOperation *operation in operations) {
      [operation finish];
    }
  }), NULL);
  gpr_free(ops_array);

  if (error != GRPC_CALL_OK) {
    [NSException raise:NSInternalInconsistencyException
                format:@"A precondition for calling grpc_call_start_batch wasn't met. Error %i",
     error];
  }
}

- (void)cancel {
  grpc_call_cancel(_call, NULL);
}

- (void)dealloc {
  grpc_call_destroy(_call);
}

@end
