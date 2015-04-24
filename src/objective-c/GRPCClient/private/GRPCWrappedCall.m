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
#import "NSDictionary+GRPC.h"
#import "NSData+GRPC.h"
#import "NSError+GRPC.h"

@implementation GRPCWrappedCall{
  grpc_call *_call;
  GRPCCompletionQueue *_queue;
}

- (instancetype)init {
  return [self initWithChannel:nil method:nil host:nil];
}

- (instancetype)initWithChannel:(GRPCChannel *)channel method:(NSString *)method host:(NSString *)host {
  if (!channel || !method || !host) {
    [NSException raise:NSInvalidArgumentException format:@"channel, method, and host cannot be nil."];
  }
  
  if (self = [super init]) {
    static dispatch_once_t initialization;
    dispatch_once(&initialization, ^{
      grpc_init();
    });
    
    _queue = [GRPCCompletionQueue completionQueue];
    _call = grpc_channel_create_call(channel.unmanagedChannel, _queue.unmanagedQueue, method.UTF8String, host.UTF8String, gpr_inf_future);
    if (_call == NULL) {
      return nil;
    }
  }
  return self;
}

- (void)startBatchWithOperations:(NSDictionary *)operations handleCompletion:(GRPCCompletionHandler)handleCompletion {
  [self startBatchWithOperations:operations handleCompletion:handleCompletion errorHandler:nil];
}

- (void)startBatchWithOperations:(NSDictionary *)operations handleCompletion:(GRPCCompletionHandler)handleCompletion errorHandler:(void (^)())errorHandler {
  size_t nops = operations.count;
  grpc_op *ops_array = gpr_malloc(nops * sizeof(grpc_op));
  size_t index = 0;
  NSMutableDictionary * __block opProcessors = [NSMutableDictionary dictionary];
  
  grpc_metadata *send_metadata = NULL;
  grpc_metadata_array *recv_initial_metadata;
  grpc_metadata_array *recv_trailing_metadata;
  grpc_byte_buffer *send_message;
  grpc_byte_buffer **recv_message = NULL;
  grpc_status_code *status_code;
  char **status_details;
  size_t *status_details_capacity;
  for (id key in operations) {
    id (^opBlock)(void);
    grpc_op *current = &ops_array[index];
    switch ([key intValue]) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        // TODO(jcanizales): Name the type of current->data.send_initial_metadata in the C library so a pointer to it can be returned from methods.
        current->data.send_initial_metadata.count = [operations[key] count];
        [operations[key] grpc_getMetadataArray:&send_metadata];
        current->data.send_initial_metadata.metadata = send_metadata;
        opBlock = ^{
          gpr_free(send_metadata);
          return @YES;
        };
        break;
      case GRPC_OP_SEND_MESSAGE:
        send_message = [operations[key] grpc_byteBuffer];
        current->data.send_message = send_message;
        opBlock = ^{
          grpc_byte_buffer_destroy(send_message);
          return @YES;
        };
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        opBlock = ^{
          return @YES;
        };
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        recv_initial_metadata = gpr_malloc(sizeof(grpc_metadata_array));
        grpc_metadata_array_init(recv_initial_metadata);
        current->data.recv_initial_metadata = recv_initial_metadata;
        opBlock = ^{
          NSDictionary *metadata = [NSDictionary grpc_dictionaryFromMetadata:recv_initial_metadata->metadata count:recv_initial_metadata->count];
          grpc_metadata_array_destroy(recv_initial_metadata);
          return metadata;
        };
        break;
      case GRPC_OP_RECV_MESSAGE:
        recv_message = gpr_malloc(sizeof(grpc_byte_buffer*));
        current->data.recv_message = recv_message;
        opBlock = ^{
          NSData *data = [NSData grpc_dataWithByteBuffer:*recv_message];
          grpc_byte_buffer_destroy(*recv_message);
          gpr_free(recv_message);
          return data;
        };
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        status_code = gpr_malloc(sizeof(status_code));
        current->data.recv_status_on_client.status = status_code;
        status_details = gpr_malloc(sizeof(char*));
        *status_details = NULL;
        current->data.recv_status_on_client.status_details = status_details;
        status_details_capacity = gpr_malloc(sizeof(grpc_status_code));
        *status_details_capacity = 0;
        current->data.recv_status_on_client.status_details_capacity = status_details_capacity;
        recv_trailing_metadata = gpr_malloc(sizeof(grpc_metadata_array));
        grpc_metadata_array_init(recv_trailing_metadata);
        current->data.recv_status_on_client.trailing_metadata = recv_trailing_metadata;
        opBlock = ^{
          grpc_status status;
          status.status = *status_code;
          status.details = *status_details;
          status.metadata = recv_trailing_metadata;
          gpr_free(status_code);
          gpr_free(status_details);
          gpr_free(status_details_capacity);
          return [NSError grpc_errorFromStatus:&status];
        };
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        [NSException raise:NSInvalidArgumentException format:@"Not a server: cannot send status"];
      default:
        [NSException raise:NSInvalidArgumentException format:@"Unrecognized dictionary key"];
    }
    current->op = [key intValue];
    opProcessors[key] = opBlock;
  }
  grpc_call_error error = grpc_call_start_batch(_call, ops_array, nops, (__bridge_retained void *)(^(grpc_op_error error){
    if (error != GRPC_OP_OK) {
      if (errorHandler) {
        errorHandler();
      } else {
        [NSException raise:@"Operation Exception" format:@"The batch failed with an unknown error"];
      }
    }
    NSMutableDictionary *result = [NSMutableDictionary dictionary];
    for (id key in opProcessors) {
      id(^block)(void) = opProcessors[key];
      id value = block();
      if (value == nil) {
        value = [NSNull null];
      }
      result[key] = value;
    }
    if (handleCompletion) {
      handleCompletion(result);
    }
  }));
  if (error != GRPC_CALL_OK) {
    [NSException raise:NSInvalidArgumentException format:@"The batch did not start successfully"];
  }
}

- (void)cancel {
  grpc_call_cancel(_call);
}

- (void)dealloc {
  grpc_call_destroy(_call);
}

@end