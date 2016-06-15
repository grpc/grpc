//
// Created by yifeit on 6/14/16.
//

#ifndef TEST_GRPC_C_UNARY_BLOCKING_CALL_H
#define TEST_GRPC_C_UNARY_BLOCKING_CALL_H

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>
#include <stdbool.h>

#define GRPC_MAX_OP_COUNT 6
#define TAG(x) ((void *)x)

typedef struct grpc_status {
  grpc_status_code code;
  char *details;
  size_t details_length;
} grpc_status;

typedef struct grpc_context {
  grpc_channel *channel;
  grpc_call *call;
  grpc_metadata* send_metadata_array;
  grpc_metadata_array recv_metadata_array;
  grpc_metadata_array trailing_metadata_array;
  // gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2);
  gpr_timespec deadline;
  grpc_byte_buffer *recv_buffer;
  grpc_status status;
} grpc_context;

typedef struct grpc_method {

} grpc_method;

typedef struct grpc_message {
  const void *data;
  size_t length;
} grpc_message;

typedef void (*grpc_op_filler)(grpc_op *op, const grpc_method *, grpc_context *, const grpc_message message, void *response);
typedef void (*grpc_op_finisher)(grpc_context *, bool *status, int max_message_size);

typedef struct grpc_op_manager {
  const grpc_op_filler fill;
  const grpc_op_finisher finish;
} grpc_op_manager;

grpc_status grpc_unary_blocking_call(grpc_channel *channel, const grpc_method *rpc_method, grpc_context *context, const grpc_message message, void *response);

#endif //TEST_GRPC_C_UNARY_BLOCKING_CALL_H
