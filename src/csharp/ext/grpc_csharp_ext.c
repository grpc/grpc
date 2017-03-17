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

#include "src/core/lib/support/string.h"

#include <grpc/byte_buffer_reader.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/slice.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include <string.h>

#ifdef GPR_WINDOWS
#define GPR_EXPORT __declspec(dllexport)
#define GPR_CALLTYPE __stdcall
#endif

#ifndef GPR_EXPORT
#define GPR_EXPORT
#endif

#ifndef GPR_CALLTYPE
#define GPR_CALLTYPE
#endif

grpc_byte_buffer *string_to_byte_buffer(const char *buffer, size_t len) {
  grpc_slice slice = grpc_slice_from_copied_buffer(buffer, len);
  grpc_byte_buffer *bb = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return bb;
}

/*
 * Helper to maintain lifetime of batch op inputs and store batch op outputs.
 */
typedef struct grpcsharp_batch_context {
  grpc_metadata_array send_initial_metadata;
  grpc_byte_buffer *send_message;
  struct {
    grpc_metadata_array trailing_metadata;
  } send_status_from_server;
  grpc_metadata_array recv_initial_metadata;
  grpc_byte_buffer *recv_message;
  struct {
    grpc_metadata_array trailing_metadata;
    grpc_status_code status;
    grpc_slice status_details;
  } recv_status_on_client;
  int recv_close_on_server_cancelled;
} grpcsharp_batch_context;

GPR_EXPORT grpcsharp_batch_context *GPR_CALLTYPE grpcsharp_batch_context_create() {
  grpcsharp_batch_context *ctx = gpr_malloc(sizeof(grpcsharp_batch_context));
  memset(ctx, 0, sizeof(grpcsharp_batch_context));
  return ctx;
}

typedef struct {
  grpc_call *call;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata;
} grpcsharp_request_call_context;

GPR_EXPORT grpcsharp_request_call_context *GPR_CALLTYPE grpcsharp_request_call_context_create() {
  grpcsharp_request_call_context *ctx = gpr_malloc(sizeof(grpcsharp_request_call_context));
  memset(ctx, 0, sizeof(grpcsharp_request_call_context));
  return ctx;
}

/*
 * Destroys array->metadata.
 * The array pointer itself is not freed.
 */
void grpcsharp_metadata_array_destroy_metadata_only(
    grpc_metadata_array *array) {
  gpr_free(array->metadata);
}

/*
 * Destroys keys, values and array->metadata.
 * The array pointer itself is not freed.
 */
void grpcsharp_metadata_array_destroy_metadata_including_entries(
    grpc_metadata_array *array) {
  size_t i;
  if (array->metadata) {
    for (i = 0; i < array->count; i++) {
      grpc_slice_unref(array->metadata[i].key);
      grpc_slice_unref(array->metadata[i].value);
    }
  }
  gpr_free(array->metadata);
}

/*
 * Fully destroys the metadata array.
 */
GPR_EXPORT void GPR_CALLTYPE
grpcsharp_metadata_array_destroy_full(grpc_metadata_array *array) {
  if (!array) {
    return;
  }
  grpcsharp_metadata_array_destroy_metadata_including_entries(array);
  gpr_free(array);
}

/*
 * Creates an empty metadata array with given capacity.
 * Array can later be destroyed by grpc_metadata_array_destroy_full.
 */
GPR_EXPORT grpc_metadata_array *GPR_CALLTYPE
grpcsharp_metadata_array_create(size_t capacity) {
  grpc_metadata_array *array =
      (grpc_metadata_array *)gpr_malloc(sizeof(grpc_metadata_array));
  grpc_metadata_array_init(array);
  array->capacity = capacity;
  array->count = 0;
  if (capacity > 0) {
    array->metadata =
        (grpc_metadata *)gpr_malloc(sizeof(grpc_metadata) * capacity);
    memset(array->metadata, 0, sizeof(grpc_metadata) * capacity);
  } else {
    array->metadata = NULL;
  }
  return array;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_metadata_array_add(grpc_metadata_array *array, const char *key,
                             const char *value, size_t value_length) {
  size_t i = array->count;
  GPR_ASSERT(array->count < array->capacity);
  array->metadata[i].key = grpc_slice_from_copied_string(key);
  array->metadata[i].value = grpc_slice_from_copied_buffer(value, value_length);
  array->count++;
}

GPR_EXPORT intptr_t GPR_CALLTYPE
grpcsharp_metadata_array_count(grpc_metadata_array *array) {
  return (intptr_t)array->count;
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_metadata_array_get_key(grpc_metadata_array *array, size_t index, size_t *key_length) {
  GPR_ASSERT(index < array->count);
  *key_length = GRPC_SLICE_LENGTH(array->metadata[index].key);
  return (char *)GRPC_SLICE_START_PTR(array->metadata[index].key);
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_metadata_array_get_value(grpc_metadata_array *array, size_t index, size_t *value_length) {
  GPR_ASSERT(index < array->count);
  *value_length = GRPC_SLICE_LENGTH(array->metadata[index].value);
  return (char *)GRPC_SLICE_START_PTR(array->metadata[index].value);
}

/* Move contents of metadata array */
void grpcsharp_metadata_array_move(grpc_metadata_array *dest,
                                   grpc_metadata_array *src) {
  if (!src) {
    dest->capacity = 0;
    dest->count = 0;
    dest->metadata = NULL;
    return;
  }

  dest->capacity = src->capacity;
  dest->count = src->count;
  dest->metadata = src->metadata;

  src->capacity = 0;
  src->count = 0;
  src->metadata = NULL;
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_batch_context_destroy(grpcsharp_batch_context *ctx) {
  if (!ctx) {
    return;
  }
  grpcsharp_metadata_array_destroy_metadata_including_entries(
      &(ctx->send_initial_metadata));

  grpc_byte_buffer_destroy(ctx->send_message);

  grpcsharp_metadata_array_destroy_metadata_including_entries(
      &(ctx->send_status_from_server.trailing_metadata));

  grpcsharp_metadata_array_destroy_metadata_only(&(ctx->recv_initial_metadata));

  grpc_byte_buffer_destroy(ctx->recv_message);

  grpcsharp_metadata_array_destroy_metadata_only(
      &(ctx->recv_status_on_client.trailing_metadata));
  grpc_slice_unref(ctx->recv_status_on_client.status_details);

  gpr_free(ctx);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_request_call_context_destroy(grpcsharp_request_call_context *ctx) {
  if (!ctx) {
    return;
  }
  /* NOTE: ctx->server_rpc_new.call is not destroyed because callback handler is
     supposed
     to take its ownership. */

  grpc_call_details_destroy(&(ctx->call_details));
  grpcsharp_metadata_array_destroy_metadata_only(
      &(ctx->request_metadata));

  gpr_free(ctx);
}

GPR_EXPORT const grpc_metadata_array *GPR_CALLTYPE
grpcsharp_batch_context_recv_initial_metadata(
    const grpcsharp_batch_context *ctx) {
  return &(ctx->recv_initial_metadata);
}

GPR_EXPORT intptr_t GPR_CALLTYPE grpcsharp_batch_context_recv_message_length(
    const grpcsharp_batch_context *ctx) {
  grpc_byte_buffer_reader reader;
  if (!ctx->recv_message) {
    return -1;
  }

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, ctx->recv_message));
  intptr_t result = (intptr_t)grpc_byte_buffer_length(reader.buffer_out);
  grpc_byte_buffer_reader_destroy(&reader);

  return result;
}

/*
 * Copies data from recv_message to a buffer. Fatal error occurs if
 * buffer is too small.
 */
GPR_EXPORT void GPR_CALLTYPE grpcsharp_batch_context_recv_message_to_buffer(
    const grpcsharp_batch_context *ctx, char *buffer, size_t buffer_len) {
  grpc_byte_buffer_reader reader;
  grpc_slice slice;
  size_t offset = 0;

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, ctx->recv_message));

  while (grpc_byte_buffer_reader_next(&reader, &slice)) {
    size_t len = GRPC_SLICE_LENGTH(slice);
    GPR_ASSERT(offset + len <= buffer_len);
    memcpy(buffer + offset, GRPC_SLICE_START_PTR(slice),
           GRPC_SLICE_LENGTH(slice));
    offset += len;
    grpc_slice_unref(slice);
  }

  grpc_byte_buffer_reader_destroy(&reader);
}

GPR_EXPORT grpc_status_code GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_status(
    const grpcsharp_batch_context *ctx) {
  return ctx->recv_status_on_client.status;
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_details(
    const grpcsharp_batch_context *ctx, size_t *details_length) {
  *details_length = GRPC_SLICE_LENGTH(ctx->recv_status_on_client.status_details);
  return (char *)GRPC_SLICE_START_PTR(ctx->recv_status_on_client.status_details);
}

GPR_EXPORT const grpc_metadata_array *GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_trailing_metadata(
    const grpcsharp_batch_context *ctx) {
  return &(ctx->recv_status_on_client.trailing_metadata);
}

GPR_EXPORT grpc_call *GPR_CALLTYPE grpcsharp_request_call_context_call(
    const grpcsharp_request_call_context *ctx) {
  return ctx->call;
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_request_call_context_method(
    const grpcsharp_request_call_context *ctx, size_t *method_length) {
  *method_length = GRPC_SLICE_LENGTH(ctx->call_details.method);
  return (char *)GRPC_SLICE_START_PTR(ctx->call_details.method);
}

GPR_EXPORT const char *GPR_CALLTYPE grpcsharp_request_call_context_host(
    const grpcsharp_request_call_context *ctx, size_t *host_length) {
  *host_length = GRPC_SLICE_LENGTH(ctx->call_details.host);
  return (char *)GRPC_SLICE_START_PTR(ctx->call_details.host);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE
grpcsharp_request_call_context_deadline(
    const grpcsharp_request_call_context *ctx) {
  return ctx->call_details.deadline;
}

GPR_EXPORT const grpc_metadata_array *GPR_CALLTYPE
grpcsharp_request_call_context_request_metadata(
    const grpcsharp_request_call_context *ctx) {
  return &(ctx->request_metadata);
}

GPR_EXPORT int32_t GPR_CALLTYPE
grpcsharp_batch_context_recv_close_on_server_cancelled(
    const grpcsharp_batch_context *ctx) {
  return (int32_t) ctx->recv_close_on_server_cancelled;
}

/* Init & shutdown */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_init(void) { grpc_init(); }

GPR_EXPORT void GPR_CALLTYPE grpcsharp_shutdown(void) { grpc_shutdown(); }

/* Completion queue */

GPR_EXPORT grpc_completion_queue *GPR_CALLTYPE
grpcsharp_completion_queue_create(void) {
  return grpc_completion_queue_create(NULL);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_shutdown(grpc_completion_queue *cq) {
  grpc_completion_queue_shutdown(cq);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_destroy(grpc_completion_queue *cq) {
  grpc_completion_queue_destroy(cq);
}

GPR_EXPORT grpc_event GPR_CALLTYPE
grpcsharp_completion_queue_next(grpc_completion_queue *cq) {
  return grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    NULL);
}

GPR_EXPORT grpc_event GPR_CALLTYPE
grpcsharp_completion_queue_pluck(grpc_completion_queue *cq, void *tag) {
  return grpc_completion_queue_pluck(cq, tag,
                                     gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
}

/* Channel */

GPR_EXPORT grpc_channel *GPR_CALLTYPE

grpcsharp_insecure_channel_create(const char *target, const grpc_channel_args *args) {
  return grpc_insecure_channel_create(target, args, NULL);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_destroy(grpc_channel *channel) {
  grpc_channel_destroy(channel);
}

GPR_EXPORT grpc_call *GPR_CALLTYPE
grpcsharp_channel_create_call(grpc_channel *channel, grpc_call *parent_call,
                              uint32_t propagation_mask,
                              grpc_completion_queue *cq,
                              const char *method, const char *host,
                              gpr_timespec deadline) {
  grpc_slice method_slice = grpc_slice_from_copied_string(method);
  grpc_slice *host_slice_ptr = NULL;
  grpc_slice host_slice;
  if (host != NULL) {
    host_slice = grpc_slice_from_copied_string(host);
    host_slice_ptr = &host_slice;
  }
  return grpc_channel_create_call(channel, parent_call, propagation_mask, cq,
                                  method_slice, host_slice_ptr, deadline, NULL);
}

GPR_EXPORT grpc_connectivity_state GPR_CALLTYPE
grpcsharp_channel_check_connectivity_state(grpc_channel *channel, int32_t try_to_connect) {
  return grpc_channel_check_connectivity_state(channel, try_to_connect);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_watch_connectivity_state(
    grpc_channel *channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue *cq, grpcsharp_batch_context *ctx) {
  grpc_channel_watch_connectivity_state(channel, last_observed_state,
                                        deadline, cq, ctx);
}

GPR_EXPORT char *GPR_CALLTYPE grpcsharp_channel_get_target(grpc_channel *channel) {
  return grpc_channel_get_target(channel);
}

/* Channel args */

GPR_EXPORT grpc_channel_args *GPR_CALLTYPE
grpcsharp_channel_args_create(size_t num_args) {
  grpc_channel_args *args =
      (grpc_channel_args *)gpr_malloc(sizeof(grpc_channel_args));
  memset(args, 0, sizeof(grpc_channel_args));

  args->num_args = num_args;
  args->args = (grpc_arg *)gpr_malloc(sizeof(grpc_arg) * num_args);
  memset(args->args, 0, sizeof(grpc_arg) * num_args);
  return args;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_channel_args_set_string(grpc_channel_args *args, size_t index,
                                  const char *key, const char *value) {
  GPR_ASSERT(args);
  GPR_ASSERT(index < args->num_args);
  args->args[index].type = GRPC_ARG_STRING;
  args->args[index].key = gpr_strdup(key);
  args->args[index].value.string = gpr_strdup(value);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_channel_args_set_integer(grpc_channel_args *args, size_t index,
                                  const char *key, int value) {
  GPR_ASSERT(args);
  GPR_ASSERT(index < args->num_args);
  args->args[index].type = GRPC_ARG_INTEGER;
  args->args[index].key = gpr_strdup(key);
  args->args[index].value.integer = value;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_channel_args_destroy(grpc_channel_args *args) {
  size_t i;
  if (args) {
    for (i = 0; i < args->num_args; i++) {
      gpr_free(args->args[i].key);
      if (args->args[i].type == GRPC_ARG_STRING) {
        gpr_free(args->args[i].value.string);
      }
    }
    gpr_free(args->args);
    gpr_free(args);
  }
}

/* Timespec */

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_now(gpr_clock_type clock_type) {
  return gpr_now(clock_type);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_inf_future(gpr_clock_type clock_type) {
  return gpr_inf_future(clock_type);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_inf_past(gpr_clock_type clock_type) {
  return gpr_inf_past(clock_type);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_convert_clock_type(gpr_timespec t, gpr_clock_type target_clock) {
  return gpr_convert_clock_type(t, target_clock);
}

GPR_EXPORT int32_t GPR_CALLTYPE gprsharp_sizeof_timespec(void) {
  return sizeof(gpr_timespec);
}

/* Call */

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_cancel(grpc_call *call) {
  return grpc_call_cancel(call, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_cancel_with_status(grpc_call *call, grpc_status_code status,
                                  const char *description) {
  return grpc_call_cancel_with_status(call, status, description, NULL);
}

GPR_EXPORT char *GPR_CALLTYPE grpcsharp_call_get_peer(grpc_call *call) {
  return grpc_call_get_peer(call);
}

GPR_EXPORT void GPR_CALLTYPE gprsharp_free(void *p) {
  gpr_free(p);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_call_destroy(grpc_call *call) {
  grpc_call_destroy(call);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_unary(grpc_call *call, grpcsharp_batch_context *ctx,
                           const char *send_buffer, size_t send_buffer_len, uint32_t write_flags,
                           grpc_metadata_array *initial_metadata, uint32_t initial_metadata_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  grpcsharp_metadata_array_move(&(ctx->send_initial_metadata),
                                initial_metadata);
  ops[0].data.send_initial_metadata.count = ctx->send_initial_metadata.count;
  ops[0].data.send_initial_metadata.metadata =
      ctx->send_initial_metadata.metadata;
  ops[0].flags = initial_metadata_flags;
  ops[0].reserved = NULL;

  ops[1].op = GRPC_OP_SEND_MESSAGE;
  ctx->send_message = string_to_byte_buffer(send_buffer, send_buffer_len);
  ops[1].data.send_message.send_message = ctx->send_message;
  ops[1].flags = write_flags;
  ops[1].reserved = NULL;

  ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[2].flags = 0;
  ops[2].reserved = NULL;

  ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[3].data.recv_initial_metadata.recv_initial_metadata =
      &(ctx->recv_initial_metadata);
  ops[3].flags = 0;
  ops[3].reserved = NULL;

  ops[4].op = GRPC_OP_RECV_MESSAGE;
  ops[4].data.recv_message.recv_message = &(ctx->recv_message);
  ops[4].flags = 0;
  ops[4].reserved = NULL;

  ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[5].data.recv_status_on_client.trailing_metadata =
      &(ctx->recv_status_on_client.trailing_metadata);
  ops[5].data.recv_status_on_client.status =
      &(ctx->recv_status_on_client.status);
  ops[5].data.recv_status_on_client.status_details =
      &(ctx->recv_status_on_client.status_details);
  ops[5].flags = 0;
  ops[5].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_client_streaming(grpc_call *call,
                                      grpcsharp_batch_context *ctx,
                                      grpc_metadata_array *initial_metadata,
                                      uint32_t initial_metadata_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[4];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  grpcsharp_metadata_array_move(&(ctx->send_initial_metadata),
                                initial_metadata);
  ops[0].data.send_initial_metadata.count = ctx->send_initial_metadata.count;
  ops[0].data.send_initial_metadata.metadata =
      ctx->send_initial_metadata.metadata;
  ops[0].flags = initial_metadata_flags;
  ops[0].reserved = NULL;

  ops[1].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[1].data.recv_initial_metadata.recv_initial_metadata =
      &(ctx->recv_initial_metadata);
  ops[1].flags = 0;
  ops[1].reserved = NULL;

  ops[2].op = GRPC_OP_RECV_MESSAGE;
  ops[2].data.recv_message.recv_message = &(ctx->recv_message);
  ops[2].flags = 0;
  ops[2].reserved = NULL;

  ops[3].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[3].data.recv_status_on_client.trailing_metadata =
      &(ctx->recv_status_on_client.trailing_metadata);
  ops[3].data.recv_status_on_client.status =
      &(ctx->recv_status_on_client.status);
  ops[3].data.recv_status_on_client.status_details =
      &(ctx->recv_status_on_client.status_details);
  ops[3].flags = 0;
  ops[3].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_start_server_streaming(
    grpc_call *call, grpcsharp_batch_context *ctx, const char *send_buffer,
    size_t send_buffer_len, uint32_t write_flags,
    grpc_metadata_array *initial_metadata, uint32_t initial_metadata_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[4];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  grpcsharp_metadata_array_move(&(ctx->send_initial_metadata),
                                initial_metadata);
  ops[0].data.send_initial_metadata.count = ctx->send_initial_metadata.count;
  ops[0].data.send_initial_metadata.metadata =
      ctx->send_initial_metadata.metadata;
  ops[0].flags = initial_metadata_flags;
  ops[0].reserved = NULL;

  ops[1].op = GRPC_OP_SEND_MESSAGE;
  ctx->send_message = string_to_byte_buffer(send_buffer, send_buffer_len);
  ops[1].data.send_message.send_message = ctx->send_message;
  ops[1].flags = write_flags;
  ops[1].reserved = NULL;

  ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[2].flags = 0;
  ops[2].reserved = NULL;

  ops[3].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[3].data.recv_status_on_client.trailing_metadata =
      &(ctx->recv_status_on_client.trailing_metadata);
  ops[3].data.recv_status_on_client.status =
      &(ctx->recv_status_on_client.status);
  ops[3].data.recv_status_on_client.status_details =
      &(ctx->recv_status_on_client.status_details);
  ops[3].flags = 0;
  ops[3].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_duplex_streaming(grpc_call *call,
                                      grpcsharp_batch_context *ctx,
                                      grpc_metadata_array *initial_metadata,
                                      uint32_t initial_metadata_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  grpcsharp_metadata_array_move(&(ctx->send_initial_metadata),
                                initial_metadata);
  ops[0].data.send_initial_metadata.count = ctx->send_initial_metadata.count;
  ops[0].data.send_initial_metadata.metadata =
      ctx->send_initial_metadata.metadata;
  ops[0].flags = initial_metadata_flags;
  ops[0].reserved = NULL;

  ops[1].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[1].data.recv_status_on_client.trailing_metadata =
      &(ctx->recv_status_on_client.trailing_metadata);
  ops[1].data.recv_status_on_client.status =
      &(ctx->recv_status_on_client.status);
  ops[1].data.recv_status_on_client.status_details =
      &(ctx->recv_status_on_client.status_details);
  ops[1].flags = 0;
  ops[1].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_recv_initial_metadata(
  grpc_call *call, grpcsharp_batch_context *ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[0].data.recv_initial_metadata.recv_initial_metadata =
      &(ctx->recv_initial_metadata);
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
    NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_send_message(grpc_call *call, grpcsharp_batch_context *ctx,
                            const char *send_buffer, size_t send_buffer_len,
                            uint32_t write_flags,
                            int32_t send_empty_initial_metadata) {
  /* TODO: don't use magic number */
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  size_t nops = send_empty_initial_metadata ? 2 : 1;
  ops[0].op = GRPC_OP_SEND_MESSAGE;
  ctx->send_message = string_to_byte_buffer(send_buffer, send_buffer_len);
  ops[0].data.send_message.send_message = ctx->send_message;
  ops[0].flags = write_flags;
  ops[0].reserved = NULL;
  ops[1].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].flags = 0;
  ops[1].reserved = NULL;

  return grpc_call_start_batch(call, ops, nops, ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_send_close_from_client(grpc_call *call,
                                      grpcsharp_batch_context *ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_send_status_from_server(
    grpc_call *call, grpcsharp_batch_context *ctx, grpc_status_code status_code,
    const char *status_details, size_t status_details_len,
    grpc_metadata_array *trailing_metadata,
    int32_t send_empty_initial_metadata, const char* optional_send_buffer,
    size_t optional_send_buffer_len, uint32_t write_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  size_t nops = 1;
  grpc_slice status_details_slice = grpc_slice_from_copied_buffer(status_details, status_details_len);
  ops[0].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  ops[0].data.send_status_from_server.status = status_code;
  ops[0].data.send_status_from_server.status_details = &status_details_slice;
  grpcsharp_metadata_array_move(
      &(ctx->send_status_from_server.trailing_metadata), trailing_metadata);
  ops[0].data.send_status_from_server.trailing_metadata_count =
      ctx->send_status_from_server.trailing_metadata.count;
  ops[0].data.send_status_from_server.trailing_metadata =
      ctx->send_status_from_server.trailing_metadata.metadata;
  ops[0].flags = 0;
  ops[0].reserved = NULL;
  if (optional_send_buffer) {
    ops[nops].op = GRPC_OP_SEND_MESSAGE;
    ctx->send_message = string_to_byte_buffer(optional_send_buffer,
                                              optional_send_buffer_len);
    ops[nops].data.send_message.send_message = ctx->send_message;
    ops[nops].flags = write_flags;
    ops[nops].reserved = NULL;
    nops++;
  }
  if (send_empty_initial_metadata) {
    ops[nops].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[nops].flags = 0;
    ops[nops].reserved = NULL;
    nops++;
  }
  return grpc_call_start_batch(call, ops, nops, ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_recv_message(grpc_call *call, grpcsharp_batch_context *ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_MESSAGE;
  ops[0].data.recv_message.recv_message = &(ctx->recv_message);
  ops[0].flags = 0;
  ops[0].reserved = NULL;
  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_serverside(grpc_call *call, grpcsharp_batch_context *ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops[0].data.recv_close_on_server.cancelled =
      (&ctx->recv_close_on_server_cancelled);
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_send_initial_metadata(grpc_call *call,
                                     grpcsharp_batch_context *ctx,
                                     grpc_metadata_array *initial_metadata) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  grpcsharp_metadata_array_move(&(ctx->send_initial_metadata),
                                initial_metadata);
  ops[0].data.send_initial_metadata.count = ctx->send_initial_metadata.count;
  ops[0].data.send_initial_metadata.metadata =
      ctx->send_initial_metadata.metadata;
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpc_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]), ctx,
                               NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_set_credentials(
    grpc_call *call,
    grpc_call_credentials *creds) {
  return grpc_call_set_credentials(call, creds);
}

/* Server */

GPR_EXPORT grpc_server *GPR_CALLTYPE
grpcsharp_server_create(const grpc_channel_args *args) {
  return grpc_server_create(args, NULL);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_server_register_completion_queue(grpc_server *server,
                                           grpc_completion_queue *cq) {
  grpc_server_register_completion_queue(server, cq, NULL);
}

GPR_EXPORT int32_t GPR_CALLTYPE
grpcsharp_server_add_insecure_http2_port(grpc_server *server, const char *addr) {
  return grpc_server_add_insecure_http2_port(server, addr);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_start(grpc_server *server) {
  grpc_server_start(server);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_server_shutdown_and_notify_callback(grpc_server *server,
                                              grpc_completion_queue *cq,
                                              grpcsharp_batch_context *ctx) {
  grpc_server_shutdown_and_notify(server, cq, ctx);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_cancel_all_calls(grpc_server *server) {
  grpc_server_cancel_all_calls(server);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_destroy(grpc_server *server) {
  grpc_server_destroy(server);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_server_request_call(grpc_server *server, grpc_completion_queue *cq,
                              grpcsharp_request_call_context *ctx) {
  return grpc_server_request_call(
      server, &(ctx->call), &(ctx->call_details),
      &(ctx->request_metadata), cq, cq, ctx);
}

/* Security */

static char *default_pem_root_certs = NULL;

static grpc_ssl_roots_override_result override_ssl_roots_handler(
    char **pem_root_certs) {
  if (!default_pem_root_certs) {
    *pem_root_certs = NULL;
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY;
  }
  *pem_root_certs = gpr_strdup(default_pem_root_certs);
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_override_default_ssl_roots(
    const char *pem_root_certs) {
  /*
   * This currently wastes ~300kB of memory by keeping a copy of roots
   * in a static variable, but for desktop/server use, the overhead
   * is negligible. In the future, we might want to change the behavior
   * for mobile (e.g. Xamarin).
   */
  default_pem_root_certs = gpr_strdup(pem_root_certs);
  grpc_set_ssl_roots_override_callback(override_ssl_roots_handler);
}

GPR_EXPORT grpc_channel_credentials *GPR_CALLTYPE
grpcsharp_ssl_credentials_create(const char *pem_root_certs,
                                 const char *key_cert_pair_cert_chain,
                                 const char *key_cert_pair_private_key) {
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  if (key_cert_pair_cert_chain || key_cert_pair_private_key) {
    key_cert_pair.cert_chain = key_cert_pair_cert_chain;
    key_cert_pair.private_key = key_cert_pair_private_key;
    return grpc_ssl_credentials_create(pem_root_certs, &key_cert_pair, NULL);
  } else {
    GPR_ASSERT(!key_cert_pair_cert_chain);
    GPR_ASSERT(!key_cert_pair_private_key);
    return grpc_ssl_credentials_create(pem_root_certs, NULL, NULL);
  }
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_credentials_release(
    grpc_channel_credentials *creds) {
  grpc_channel_credentials_release(creds);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_call_credentials_release(
    grpc_call_credentials *creds) {
  grpc_call_credentials_release(creds);
}

GPR_EXPORT grpc_channel *GPR_CALLTYPE
grpcsharp_secure_channel_create(grpc_channel_credentials *creds,
                                const char *target,
                                const grpc_channel_args *args) {
  return grpc_secure_channel_create(creds, target, args, NULL);
}

GPR_EXPORT grpc_server_credentials *GPR_CALLTYPE
grpcsharp_ssl_server_credentials_create(
    const char *pem_root_certs, const char **key_cert_pair_cert_chain_array,
    const char **key_cert_pair_private_key_array, size_t num_key_cert_pairs,
    int force_client_auth) {
  size_t i;
  grpc_server_credentials *creds;
  grpc_ssl_pem_key_cert_pair *key_cert_pairs =
      gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair) * num_key_cert_pairs);
  memset(key_cert_pairs, 0,
         sizeof(grpc_ssl_pem_key_cert_pair) * num_key_cert_pairs);

  for (i = 0; i < num_key_cert_pairs; i++) {
    if (key_cert_pair_cert_chain_array[i] ||
        key_cert_pair_private_key_array[i]) {
      key_cert_pairs[i].cert_chain = key_cert_pair_cert_chain_array[i];
      key_cert_pairs[i].private_key = key_cert_pair_private_key_array[i];
    }
  }
  creds = grpc_ssl_server_credentials_create_ex(
      pem_root_certs, key_cert_pairs, num_key_cert_pairs,
      force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
      NULL);
  gpr_free(key_cert_pairs);
  return creds;
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_credentials_release(
    grpc_server_credentials *creds) {
  grpc_server_credentials_release(creds);
}

GPR_EXPORT int32_t GPR_CALLTYPE
grpcsharp_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                       grpc_server_credentials *creds) {
  return grpc_server_add_secure_http2_port(server, addr, creds);
}

GPR_EXPORT grpc_channel_credentials *GPR_CALLTYPE grpcsharp_composite_channel_credentials_create(
  grpc_channel_credentials *channel_creds,
  grpc_call_credentials *call_creds) {
  return grpc_composite_channel_credentials_create(channel_creds, call_creds, NULL);
}

GPR_EXPORT grpc_call_credentials *GPR_CALLTYPE grpcsharp_composite_call_credentials_create(
  grpc_call_credentials *creds1,
  grpc_call_credentials *creds2) {
  return grpc_composite_call_credentials_create(creds1, creds2, NULL);
}


/* Metadata credentials plugin */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_metadata_credentials_notify_from_plugin(
    grpc_credentials_plugin_metadata_cb cb,
    void *user_data, grpc_metadata_array *metadata,
  grpc_status_code status, const char *error_details) {
  if (metadata) {
    cb(user_data, metadata->metadata, metadata->count, status, error_details);
  } else {
    cb(user_data, NULL, 0, status, error_details);
  }
}

typedef void(GPR_CALLTYPE *grpcsharp_metadata_interceptor_func)(
  void *state, const char *service_url, const char *method_name,
  grpc_credentials_plugin_metadata_cb cb,
  void *user_data, int32_t is_destroy);

static void grpcsharp_get_metadata_handler(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data) {
  grpcsharp_metadata_interceptor_func interceptor =
      (grpcsharp_metadata_interceptor_func)(intptr_t)state;
  interceptor(state, context.service_url, context.method_name, cb, user_data, 0);
}

static void grpcsharp_metadata_credentials_destroy_handler(void *state) {
  grpcsharp_metadata_interceptor_func interceptor =
      (grpcsharp_metadata_interceptor_func)(intptr_t)state;
  interceptor(state, NULL, NULL, NULL, NULL, 1);
}

GPR_EXPORT grpc_call_credentials *GPR_CALLTYPE grpcsharp_metadata_credentials_create_from_plugin(
  grpcsharp_metadata_interceptor_func metadata_interceptor) {
  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = grpcsharp_get_metadata_handler;
  plugin.destroy = grpcsharp_metadata_credentials_destroy_handler;
  plugin.state = (void*)(intptr_t)metadata_interceptor;
  plugin.type = "";
  return grpc_metadata_credentials_create_from_plugin(plugin, NULL);
}

/* Auth context */

GPR_EXPORT grpc_auth_context *GPR_CALLTYPE grpcsharp_call_auth_context(grpc_call *call) {
  return grpc_call_auth_context(call);
}

GPR_EXPORT const char *GPR_CALLTYPE grpcsharp_auth_context_peer_identity_property_name(
    const grpc_auth_context *ctx) {
  return grpc_auth_context_peer_identity_property_name(ctx);
}

GPR_EXPORT grpc_auth_property_iterator GPR_CALLTYPE
grpcsharp_auth_context_property_iterator(const grpc_auth_context *ctx) {
  return grpc_auth_context_property_iterator(ctx);
}

GPR_EXPORT const grpc_auth_property *GPR_CALLTYPE grpcsharp_auth_property_iterator_next(
    grpc_auth_property_iterator *it) {
  return grpc_auth_property_iterator_next(it);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_auth_context_release(grpc_auth_context *ctx) {
  grpc_auth_context_release(ctx);
}

/* Logging */

typedef void(GPR_CALLTYPE *grpcsharp_log_func)(const char *file, int32_t line,
                                               uint64_t thd_id,
                                               const char *severity_string,
                                               const char *msg);
static grpcsharp_log_func log_func = NULL;

/* Redirects gpr_log to log_func callback */
static void grpcsharp_log_handler(gpr_log_func_args *args) {
  log_func(args->file, args->line, gpr_thd_currentid(),
           gpr_log_severity_string(args->severity), args->message);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_redirect_log(grpcsharp_log_func func) {
  GPR_ASSERT(func);
  log_func = func;
  gpr_set_log_function(grpcsharp_log_handler);
}

typedef void(GPR_CALLTYPE *test_callback_funcptr)(int32_t success);

/* Version info */
GPR_EXPORT const char *GPR_CALLTYPE grpcsharp_version_string() {
  return grpc_version_string();
}

/* For testing */
GPR_EXPORT void GPR_CALLTYPE
grpcsharp_test_callback(test_callback_funcptr callback) {
  callback(1);
}

/* For testing */
GPR_EXPORT void *GPR_CALLTYPE grpcsharp_test_nop(void *ptr) { return ptr; }

/* For testing */
GPR_EXPORT int32_t GPR_CALLTYPE grpcsharp_sizeof_grpc_event(void) {
  return sizeof(grpc_event);
}
