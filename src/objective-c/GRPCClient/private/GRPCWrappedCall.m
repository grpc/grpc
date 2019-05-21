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

#import "GRPCWrappedCall.h"

#import <Foundation/Foundation.h>
#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#import "GRPCChannel.h"
#import "GRPCChannelPool.h"
#import "GRPCCompletionQueue.h"
#import "GRPCHost.h"
#import "NSData+GRPC.h"
#import "NSDictionary+GRPC.h"
#import "NSError+GRPC.h"

#import "GRPCOpBatchLog.h"

@implementation GRPCOperation {
 @protected
  // Most operation subclasses don't set any flags in the grpc_op, and rely on the flag member being
  // initialized to zero.
  grpc_op _op;
  void (^_handler)(void);
}

- (void)finish {
  if (_handler) {
    void (^handler)(void) = _handler;
    _handler = nil;
    handler();
  }
}
@end

@implementation GRPCOpSendMetadata

- (instancetype)init {
  return [self initWithMetadata:nil flags:0 handler:nil];
}

- (instancetype)initWithMetadata:(NSDictionary *)metadata handler:(void (^)(void))handler {
  return [self initWithMetadata:metadata flags:0 handler:handler];
}

- (instancetype)initWithMetadata:(NSDictionary *)metadata
                           flags:(uint32_t)flags
                         handler:(void (^)(void))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_SEND_INITIAL_METADATA;
    _op.data.send_initial_metadata.count = metadata.count;
    _op.data.send_initial_metadata.metadata = metadata.grpc_metadataArray;
    _op.data.send_initial_metadata.maybe_compression_level.is_set = false;
    _op.data.send_initial_metadata.maybe_compression_level.level = 0;
    _op.flags = flags;
    _handler = handler;
  }
  return self;
}

- (void)dealloc {
  for (int i = 0; i < _op.data.send_initial_metadata.count; i++) {
    grpc_slice_unref(_op.data.send_initial_metadata.metadata[i].key);
    grpc_slice_unref(_op.data.send_initial_metadata.metadata[i].value);
  }
  gpr_free(_op.data.send_initial_metadata.metadata);
}

@end

@implementation GRPCOpSendMessage

- (instancetype)init {
  return [self initWithMessage:nil handler:nil];
}

- (instancetype)initWithMessage:(NSData *)message handler:(void (^)(void))handler {
  if (!message) {
    [NSException raise:NSInvalidArgumentException format:@"message cannot be nil"];
  }
  if (self = [super init]) {
    _op.op = GRPC_OP_SEND_MESSAGE;
    _op.data.send_message.send_message = message.grpc_byteBuffer;
    _handler = handler;
  }
  return self;
}

- (void)dealloc {
  grpc_byte_buffer_destroy(_op.data.send_message.send_message);
}

@end

@implementation GRPCOpSendClose

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)(void))handler {
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

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)(NSDictionary *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_INITIAL_METADATA;
    grpc_metadata_array_init(&_headers);
    _op.data.recv_initial_metadata.recv_initial_metadata = &_headers;
    if (handler) {
      // Prevent reference cycle with _handler
      __weak typeof(self) weakSelf = self;
      _handler = ^{
        __strong typeof(self) strongSelf = weakSelf;
        NSDictionary *metadata =
            [NSDictionary grpc_dictionaryFromMetadataArray:strongSelf->_headers];
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

@implementation GRPCOpRecvMessage {
  grpc_byte_buffer *_receivedMessage;
}

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)(grpc_byte_buffer *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_MESSAGE;
    _op.data.recv_message.recv_message = &_receivedMessage;
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

@implementation GRPCOpRecvStatus {
  grpc_status_code _statusCode;
  grpc_slice _details;
  size_t _detailsCapacity;
  grpc_metadata_array _trailers;
  const char *_errorString;
}

- (instancetype)init {
  return [self initWithHandler:nil];
}

- (instancetype)initWithHandler:(void (^)(NSError *, NSDictionary *))handler {
  if (self = [super init]) {
    _op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    _op.data.recv_status_on_client.status = &_statusCode;
    _op.data.recv_status_on_client.status_details = &_details;
    grpc_metadata_array_init(&_trailers);
    _op.data.recv_status_on_client.trailing_metadata = &_trailers;
    _op.data.recv_status_on_client.error_string = &_errorString;
    if (handler) {
      // Prevent reference cycle with _handler
      __weak typeof(self) weakSelf = self;
      _handler = ^{
        __strong typeof(self) strongSelf = weakSelf;
        if (strongSelf) {
          char *details = grpc_slice_to_c_string(strongSelf->_details);
          NSError *error = [NSError grpc_errorFromStatusCode:strongSelf->_statusCode
                                                     details:details
                                                 errorString:strongSelf->_errorString];
          NSDictionary *trailers =
              [NSDictionary grpc_dictionaryFromMetadataArray:strongSelf->_trailers];
          handler(error, trailers);
          gpr_free(details);
        }
      };
    }
  }
  return self;
}

- (void)dealloc {
  grpc_metadata_array_destroy(&_trailers);
  grpc_slice_unref(_details);
  gpr_free((void *)_errorString);
}

@end

#pragma mark GRPCWrappedCall

@implementation GRPCWrappedCall {
  // pooledChannel holds weak reference to this object so this is ok
  GRPCPooledChannel *_pooledChannel;
  grpc_call *_call;
}

- (instancetype)initWithUnmanagedCall:(grpc_call *)unmanagedCall
                        pooledChannel:(GRPCPooledChannel *)pooledChannel {
  NSAssert(unmanagedCall != NULL, @"unmanagedCall cannot be empty.");
  NSAssert(pooledChannel != nil, @"pooledChannel cannot be empty.");
  if (unmanagedCall == NULL || pooledChannel == nil) {
    return nil;
  }

  if ((self = [super init])) {
    _call = unmanagedCall;
    _pooledChannel = pooledChannel;
  }
  return self;
}

- (void)startBatchWithOperations:(NSArray *)operations {
  [self startBatchWithOperations:operations errorHandler:nil];
}

- (void)startBatchWithOperations:(NSArray *)operations errorHandler:(void (^)(void))errorHandler {
// Keep logs of op batches when we are running tests. Disabled when in production for improved
// performance.
#ifdef GRPC_TEST_OBJC
  [GRPCOpBatchLog addOpBatchToLog:operations];
#endif

  @synchronized(self) {
    if (_call != NULL) {
      size_t nops = operations.count;
      grpc_op *ops_array = gpr_malloc(nops * sizeof(grpc_op));
      size_t i = 0;
      for (GRPCOperation *operation in operations) {
        ops_array[i++] = operation.op;
      }
      grpc_call_error error =
          grpc_call_start_batch(_call, ops_array, nops, (__bridge_retained void *)(^(bool success) {
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
                                }),
                                NULL);
      gpr_free(ops_array);

      NSAssert(error == GRPC_CALL_OK, @"Error starting a batch of operations: %i", error);
      // To avoid compiler complaint when NSAssert is disabled.
      if (error != GRPC_CALL_OK) {
        return;
      }
    }
  }
}

- (void)cancel {
  @synchronized(self) {
    if (_call != NULL) {
      grpc_call_cancel(_call, NULL);
    }
  }
}

- (void)channelDisconnected {
  @synchronized(self) {
    if (_call != NULL) {
      // Unreference the call will lead to its cancellation in the core. Note that since
      // this function is only called with a network state change, any existing GRPCCall object will
      // also receive the same notification and cancel themselves with GRPCErrorCodeUnavailable, so
      // the user gets GRPCErrorCodeUnavailable in this case.
      grpc_call_unref(_call);
      _call = NULL;
    }
  }
}

- (void)dealloc {
  @synchronized(self) {
    if (_call != NULL) {
      grpc_call_unref(_call);
      _call = NULL;
    }
  }
  // Explicitly converting weak reference _pooledChannel to strong.
  __strong GRPCPooledChannel *channel = _pooledChannel;
  [channel notifyWrappedCallDealloc:self];
}

@end
