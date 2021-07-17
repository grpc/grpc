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

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd_id.h>

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

static grpc_byte_buffer* grpcsharp_create_byte_buffer_from_stolen_slices(
    grpc_slice_buffer* slice_buffer) {
  grpc_byte_buffer* bb =
      (grpc_byte_buffer*)gpr_zalloc(sizeof(grpc_byte_buffer));
  bb->type = GRPC_BB_RAW;
  bb->data.raw.compression = GRPC_COMPRESS_NONE;
  grpc_slice_buffer_init(&bb->data.raw.slice_buffer);

  grpc_slice_buffer_swap(&bb->data.raw.slice_buffer, slice_buffer);
  return bb;
}

/*
 * Helper to maintain lifetime of batch op inputs and store batch op outputs.
 */
typedef struct grpcsharp_batch_context {
  grpc_metadata_array send_initial_metadata;
  grpc_byte_buffer* send_message;
  struct {
    grpc_metadata_array trailing_metadata;
  } send_status_from_server;
  grpc_metadata_array recv_initial_metadata;
  grpc_byte_buffer* recv_message;
  grpc_byte_buffer_reader* recv_message_reader;
  struct {
    grpc_metadata_array trailing_metadata;
    grpc_status_code status;
    grpc_slice status_details;
    const char* error_string;
  } recv_status_on_client;
  int recv_close_on_server_cancelled;

  /* reserve space for byte_buffer_reader */
  grpc_byte_buffer_reader reserved_recv_message_reader;
} grpcsharp_batch_context;

GPR_EXPORT grpcsharp_batch_context* GPR_CALLTYPE
grpcsharp_batch_context_create() {
  grpcsharp_batch_context* ctx = gpr_malloc(sizeof(grpcsharp_batch_context));
  memset(ctx, 0, sizeof(grpcsharp_batch_context));
  return ctx;
}

typedef struct {
  grpc_call* call;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata;
} grpcsharp_request_call_context;

GPR_EXPORT grpcsharp_request_call_context* GPR_CALLTYPE
grpcsharp_request_call_context_create() {
  grpcsharp_request_call_context* ctx =
      gpr_malloc(sizeof(grpcsharp_request_call_context));
  memset(ctx, 0, sizeof(grpcsharp_request_call_context));
  return ctx;
}

/*
 * Destroys array->metadata.
 * The array pointer itself is not freed.
 */
void grpcsharp_metadata_array_destroy_metadata_only(
    grpc_metadata_array* array) {
  gpr_free(array->metadata);
}

/*
 * Destroys keys, values and array->metadata.
 * The array pointer itself is not freed.
 */
void grpcsharp_metadata_array_destroy_metadata_including_entries(
    grpc_metadata_array* array) {
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
grpcsharp_metadata_array_destroy_full(grpc_metadata_array* array) {
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
GPR_EXPORT grpc_metadata_array* GPR_CALLTYPE
grpcsharp_metadata_array_create(size_t capacity) {
  grpc_metadata_array* array =
      (grpc_metadata_array*)gpr_malloc(sizeof(grpc_metadata_array));
  grpc_metadata_array_init(array);
  array->capacity = capacity;
  array->count = 0;
  if (capacity > 0) {
    array->metadata =
        (grpc_metadata*)gpr_malloc(sizeof(grpc_metadata) * capacity);
    memset(array->metadata, 0, sizeof(grpc_metadata) * capacity);
  } else {
    array->metadata = NULL;
  }
  return array;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_metadata_array_add(grpc_metadata_array* array, const char* key,
                             const char* value, size_t value_length) {
  size_t i = array->count;
  GPR_ASSERT(array->count < array->capacity);
  array->metadata[i].key = grpc_slice_from_copied_string(key);
  array->metadata[i].value = grpc_slice_from_copied_buffer(value, value_length);
  array->count++;
}

GPR_EXPORT intptr_t GPR_CALLTYPE
grpcsharp_metadata_array_count(grpc_metadata_array* array) {
  return (intptr_t)array->count;
}

GPR_EXPORT const char* GPR_CALLTYPE grpcsharp_metadata_array_get_key(
    grpc_metadata_array* array, size_t index, size_t* key_length) {
  GPR_ASSERT(index < array->count);
  *key_length = GRPC_SLICE_LENGTH(array->metadata[index].key);
  return (char*)GRPC_SLICE_START_PTR(array->metadata[index].key);
}

GPR_EXPORT const char* GPR_CALLTYPE grpcsharp_metadata_array_get_value(
    grpc_metadata_array* array, size_t index, size_t* value_length) {
  GPR_ASSERT(index < array->count);
  *value_length = GRPC_SLICE_LENGTH(array->metadata[index].value);
  return (char*)GRPC_SLICE_START_PTR(array->metadata[index].value);
}

/* Move contents of metadata array */
void grpcsharp_metadata_array_move(grpc_metadata_array* dest,
                                   grpc_metadata_array* src) {
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

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_batch_context_reset(grpcsharp_batch_context* ctx) {
  grpcsharp_metadata_array_destroy_metadata_including_entries(
      &(ctx->send_initial_metadata));

  grpc_byte_buffer_destroy(ctx->send_message);

  grpcsharp_metadata_array_destroy_metadata_including_entries(
      &(ctx->send_status_from_server.trailing_metadata));

  grpcsharp_metadata_array_destroy_metadata_only(&(ctx->recv_initial_metadata));

  if (ctx->recv_message_reader) {
    grpc_byte_buffer_reader_destroy(ctx->recv_message_reader);
  }
  grpc_byte_buffer_destroy(ctx->recv_message);

  grpcsharp_metadata_array_destroy_metadata_only(
      &(ctx->recv_status_on_client.trailing_metadata));
  grpc_slice_unref(ctx->recv_status_on_client.status_details);
  gpr_free((void*)ctx->recv_status_on_client.error_string);
  memset(ctx, 0, sizeof(grpcsharp_batch_context));
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_batch_context_destroy(grpcsharp_batch_context* ctx) {
  if (!ctx) {
    return;
  }
  grpcsharp_batch_context_reset(ctx);
  gpr_free(ctx);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_request_call_context_reset(grpcsharp_request_call_context* ctx) {
  /* NOTE: ctx->server_rpc_new.call is not destroyed because callback handler is
     supposed
     to take its ownership. */

  grpc_call_details_destroy(&(ctx->call_details));
  grpcsharp_metadata_array_destroy_metadata_only(&(ctx->request_metadata));
  memset(ctx, 0, sizeof(grpcsharp_request_call_context));
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_request_call_context_destroy(grpcsharp_request_call_context* ctx) {
  if (!ctx) {
    return;
  }
  grpcsharp_request_call_context_reset(ctx);
  gpr_free(ctx);
}

GPR_EXPORT const grpc_metadata_array* GPR_CALLTYPE
grpcsharp_batch_context_recv_initial_metadata(
    const grpcsharp_batch_context* ctx) {
  return &(ctx->recv_initial_metadata);
}

GPR_EXPORT intptr_t GPR_CALLTYPE grpcsharp_batch_context_recv_message_length(
    const grpcsharp_batch_context* ctx) {
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
 * Gets the next slice from recv_message byte buffer.
 * Returns 1 if a slice was get successfully, 0 if there are no more slices to
 * read. Set slice_len to the length of the slice and the slice_data_ptr to
 * point to slice's data. Caller must ensure that the byte buffer being read
 * from stays alive as long as the data of the slice are being accessed
 * (grpc_byte_buffer_reader_peek method is used internally)
 *
 * Remarks:
 * Slices can only be iterated once.
 * Initializes recv_message_buffer_reader if it was not initialized yet.
 */
GPR_EXPORT int GPR_CALLTYPE
grpcsharp_batch_context_recv_message_next_slice_peek(
    grpcsharp_batch_context* ctx, size_t* slice_len, uint8_t** slice_data_ptr) {
  *slice_len = 0;
  *slice_data_ptr = NULL;

  if (!ctx->recv_message) {
    return 0;
  }

  if (!ctx->recv_message_reader) {
    ctx->recv_message_reader = &ctx->reserved_recv_message_reader;
    GPR_ASSERT(grpc_byte_buffer_reader_init(ctx->recv_message_reader,
                                            ctx->recv_message));
  }

  grpc_slice* slice_ptr;
  if (!grpc_byte_buffer_reader_peek(ctx->recv_message_reader, &slice_ptr)) {
    return 0;
  }

  /* recv_message buffer must not be deleted before all the data is read */
  *slice_len = GRPC_SLICE_LENGTH(*slice_ptr);
  *slice_data_ptr = GRPC_SLICE_START_PTR(*slice_ptr);
  return 1;
}

GPR_EXPORT grpc_status_code GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_status(
    const grpcsharp_batch_context* ctx) {
  return ctx->recv_status_on_client.status;
}

GPR_EXPORT const char* GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_details(
    const grpcsharp_batch_context* ctx, size_t* details_length) {
  *details_length =
      GRPC_SLICE_LENGTH(ctx->recv_status_on_client.status_details);
  return (char*)GRPC_SLICE_START_PTR(ctx->recv_status_on_client.status_details);
}

GPR_EXPORT const char* GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_error_string(
    const grpcsharp_batch_context* ctx) {
  return ctx->recv_status_on_client.error_string;
}

GPR_EXPORT const grpc_metadata_array* GPR_CALLTYPE
grpcsharp_batch_context_recv_status_on_client_trailing_metadata(
    const grpcsharp_batch_context* ctx) {
  return &(ctx->recv_status_on_client.trailing_metadata);
}

GPR_EXPORT grpc_call* GPR_CALLTYPE
grpcsharp_request_call_context_call(const grpcsharp_request_call_context* ctx) {
  return ctx->call;
}

GPR_EXPORT const char* GPR_CALLTYPE grpcsharp_request_call_context_method(
    const grpcsharp_request_call_context* ctx, size_t* method_length) {
  *method_length = GRPC_SLICE_LENGTH(ctx->call_details.method);
  return (char*)GRPC_SLICE_START_PTR(ctx->call_details.method);
}

GPR_EXPORT const char* GPR_CALLTYPE grpcsharp_request_call_context_host(
    const grpcsharp_request_call_context* ctx, size_t* host_length) {
  *host_length = GRPC_SLICE_LENGTH(ctx->call_details.host);
  return (char*)GRPC_SLICE_START_PTR(ctx->call_details.host);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE grpcsharp_request_call_context_deadline(
    const grpcsharp_request_call_context* ctx) {
  return ctx->call_details.deadline;
}

GPR_EXPORT const grpc_metadata_array* GPR_CALLTYPE
grpcsharp_request_call_context_request_metadata(
    const grpcsharp_request_call_context* ctx) {
  return &(ctx->request_metadata);
}

GPR_EXPORT int32_t GPR_CALLTYPE
grpcsharp_batch_context_recv_close_on_server_cancelled(
    const grpcsharp_batch_context* ctx) {
  return (int32_t)ctx->recv_close_on_server_cancelled;
}

/* Init & shutdown */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_init(void) { grpc_init(); }

GPR_EXPORT void GPR_CALLTYPE grpcsharp_shutdown(void) { grpc_shutdown(); }

/* Completion queue */

GPR_EXPORT grpc_completion_queue* GPR_CALLTYPE
grpcsharp_completion_queue_create_async(void) {
  return grpc_completion_queue_create_for_next(NULL);
}

GPR_EXPORT grpc_completion_queue* GPR_CALLTYPE
grpcsharp_completion_queue_create_sync(void) {
  return grpc_completion_queue_create_for_pluck(NULL);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_shutdown(grpc_completion_queue* cq) {
  grpc_completion_queue_shutdown(cq);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_destroy(grpc_completion_queue* cq) {
  grpc_completion_queue_destroy(cq);
}

GPR_EXPORT grpc_event GPR_CALLTYPE
grpcsharp_completion_queue_next(grpc_completion_queue* cq) {
  return grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    NULL);
}

GPR_EXPORT grpc_event GPR_CALLTYPE
grpcsharp_completion_queue_pluck(grpc_completion_queue* cq, void* tag) {
  return grpc_completion_queue_pluck(cq, tag,
                                     gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
}

/* Channel */

GPR_EXPORT grpc_channel* GPR_CALLTYPE

grpcsharp_insecure_channel_create(const char* target,
                                  const grpc_channel_args* args) {
  return grpc_insecure_channel_create(target, args, NULL);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_destroy(grpc_channel* channel) {
  grpc_channel_destroy(channel);
}

GPR_EXPORT grpc_call* GPR_CALLTYPE grpcsharp_channel_create_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, const char* method, const char* host,
    gpr_timespec deadline) {
  grpc_slice method_slice = grpc_slice_from_copied_string(method);
  grpc_slice* host_slice_ptr = NULL;
  grpc_slice host_slice;
  if (host != NULL) {
    host_slice = grpc_slice_from_copied_string(host);
    host_slice_ptr = &host_slice;
  }
  grpc_call* ret =
      grpc_channel_create_call(channel, parent_call, propagation_mask, cq,
                               method_slice, host_slice_ptr, deadline, NULL);
  grpc_slice_unref(method_slice);
  if (host != NULL) {
    grpc_slice_unref(host_slice);
  }
  return ret;
}

GPR_EXPORT grpc_connectivity_state GPR_CALLTYPE
grpcsharp_channel_check_connectivity_state(grpc_channel* channel,
                                           int32_t try_to_connect) {
  return grpc_channel_check_connectivity_state(channel, try_to_connect);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_watch_connectivity_state(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq,
    grpcsharp_batch_context* ctx) {
  grpc_channel_watch_connectivity_state(channel, last_observed_state, deadline,
                                        cq, ctx);
}

GPR_EXPORT char* GPR_CALLTYPE
grpcsharp_channel_get_target(grpc_channel* channel) {
  return grpc_channel_get_target(channel);
}

/* Channel args */

GPR_EXPORT grpc_channel_args* GPR_CALLTYPE
grpcsharp_channel_args_create(size_t num_args) {
  grpc_channel_args* args =
      (grpc_channel_args*)gpr_malloc(sizeof(grpc_channel_args));
  memset(args, 0, sizeof(grpc_channel_args));

  args->num_args = num_args;
  args->args = (grpc_arg*)gpr_malloc(sizeof(grpc_arg) * num_args);
  memset(args->args, 0, sizeof(grpc_arg) * num_args);
  return args;
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_args_set_string(
    grpc_channel_args* args, size_t index, const char* key, const char* value) {
  GPR_ASSERT(args);
  GPR_ASSERT(index < args->num_args);
  args->args[index].type = GRPC_ARG_STRING;
  args->args[index].key = gpr_strdup(key);
  args->args[index].value.string = gpr_strdup(value);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_args_set_integer(
    grpc_channel_args* args, size_t index, const char* key, int value) {
  GPR_ASSERT(args);
  GPR_ASSERT(index < args->num_args);
  args->args[index].type = GRPC_ARG_INTEGER;
  args->args[index].key = gpr_strdup(key);
  args->args[index].value.integer = value;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_channel_args_destroy(grpc_channel_args* args) {
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

GPR_EXPORT gpr_timespec GPR_CALLTYPE
gprsharp_inf_future(gpr_clock_type clock_type) {
  return gpr_inf_future(clock_type);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE
gprsharp_inf_past(gpr_clock_type clock_type) {
  return gpr_inf_past(clock_type);
}

GPR_EXPORT gpr_timespec GPR_CALLTYPE
gprsharp_convert_clock_type(gpr_timespec t, gpr_clock_type target_clock) {
  return gpr_convert_clock_type(t, target_clock);
}

GPR_EXPORT int32_t GPR_CALLTYPE gprsharp_sizeof_timespec(void) {
  return sizeof(gpr_timespec);
}

/* Call */

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_cancel(grpc_call* call) {
  return grpc_call_cancel(call, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_cancel_with_status(
    grpc_call* call, grpc_status_code status, const char* description) {
  return grpc_call_cancel_with_status(call, status, description, NULL);
}

GPR_EXPORT char* GPR_CALLTYPE grpcsharp_call_get_peer(grpc_call* call) {
  return grpc_call_get_peer(call);
}

GPR_EXPORT void GPR_CALLTYPE gprsharp_free(void* p) { gpr_free(p); }

GPR_EXPORT void GPR_CALLTYPE grpcsharp_call_destroy(grpc_call* call) {
  grpc_call_unref(call);
}

typedef grpc_call_error (*grpcsharp_call_start_batch_func)(grpc_call* call,
                                                           const grpc_op* ops,
                                                           size_t nops,
                                                           void* tag,
                                                           void* reserved);

/* Only for testing */
static grpc_call_error grpcsharp_call_start_batch_nop(grpc_call* call,
                                                      const grpc_op* ops,
                                                      size_t nops, void* tag,
                                                      void* reserved) {
  (void)call;
  (void)ops;
  (void)nops;
  (void)tag;
  (void)reserved;
  return GRPC_CALL_OK;
}

static grpc_call_error grpcsharp_call_start_batch_default(grpc_call* call,
                                                          const grpc_op* ops,
                                                          size_t nops,
                                                          void* tag,
                                                          void* reserved) {
  return grpc_call_start_batch(call, ops, nops, tag, reserved);
}

static grpcsharp_call_start_batch_func g_call_start_batch_func =
    grpcsharp_call_start_batch_default;

static grpc_call_error grpcsharp_call_start_batch(grpc_call* call,
                                                  const grpc_op* ops,
                                                  size_t nops, void* tag,
                                                  void* reserved) {
  return g_call_start_batch_func(call, ops, nops, tag, reserved);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_start_unary(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_slice_buffer* send_buffer, uint32_t write_flags,
    grpc_metadata_array* initial_metadata, uint32_t initial_metadata_flags) {
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
  ctx->send_message =
      grpcsharp_create_byte_buffer_from_stolen_slices(send_buffer);
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
  ops[5].data.recv_status_on_client.error_string =
      &(ctx->recv_status_on_client.error_string);
  ops[5].flags = 0;
  ops[5].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

/* Only for testing. Shortcircuits the unary call logic and only echoes the
   message as if it was received from the server */
GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_test_call_start_unary_echo(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_slice_buffer* send_buffer, uint32_t write_flags,
    grpc_metadata_array* initial_metadata, uint32_t initial_metadata_flags) {
  (void)call;
  (void)write_flags;
  (void)initial_metadata_flags;
  // prepare as if we were performing a normal RPC.
  grpc_byte_buffer* send_message =
      grpcsharp_create_byte_buffer_from_stolen_slices(send_buffer);

  ctx->recv_message = send_message;  // echo message sent by the client as if
                                     // received from server.
  ctx->recv_status_on_client.status = GRPC_STATUS_OK;
  ctx->recv_status_on_client.status_details = grpc_empty_slice();
  ctx->recv_status_on_client.error_string = NULL;
  // echo initial metadata as if received from server (as trailing metadata)
  grpcsharp_metadata_array_move(&(ctx->recv_status_on_client.trailing_metadata),
                                initial_metadata);
  return GRPC_CALL_OK;
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_start_client_streaming(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_metadata_array* initial_metadata, uint32_t initial_metadata_flags) {
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
  ops[3].data.recv_status_on_client.error_string =
      &(ctx->recv_status_on_client.error_string);
  ops[3].flags = 0;
  ops[3].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_start_server_streaming(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_slice_buffer* send_buffer, uint32_t write_flags,
    grpc_metadata_array* initial_metadata, uint32_t initial_metadata_flags) {
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
  ctx->send_message =
      grpcsharp_create_byte_buffer_from_stolen_slices(send_buffer);
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
  ops[3].data.recv_status_on_client.error_string =
      &(ctx->recv_status_on_client.error_string);
  ops[3].flags = 0;
  ops[3].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_start_duplex_streaming(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_metadata_array* initial_metadata, uint32_t initial_metadata_flags) {
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
  ops[1].data.recv_status_on_client.error_string =
      &(ctx->recv_status_on_client.error_string);
  ops[1].flags = 0;
  ops[1].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_recv_initial_metadata(
    grpc_call* call, grpcsharp_batch_context* ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[0].data.recv_initial_metadata.recv_initial_metadata =
      &(ctx->recv_initial_metadata);
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_send_message(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_slice_buffer* send_buffer, uint32_t write_flags,
    int32_t send_empty_initial_metadata) {
  /* TODO: don't use magic number */
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  size_t nops = send_empty_initial_metadata ? 2 : 1;
  ops[0].op = GRPC_OP_SEND_MESSAGE;
  ctx->send_message =
      grpcsharp_create_byte_buffer_from_stolen_slices(send_buffer);
  ops[0].data.send_message.send_message = ctx->send_message;
  ops[0].flags = write_flags;
  ops[0].reserved = NULL;
  ops[1].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].flags = 0;
  ops[1].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, nops, ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_send_close_from_client(
    grpc_call* call, grpcsharp_batch_context* ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_send_status_from_server(
    grpc_call* call, grpcsharp_batch_context* ctx, grpc_status_code status_code,
    const char* status_details, size_t status_details_len,
    grpc_metadata_array* trailing_metadata, int32_t send_empty_initial_metadata,
    grpc_slice_buffer* optional_send_buffer, uint32_t write_flags) {
  /* TODO: don't use magic number */
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  size_t nops = 1;
  grpc_slice status_details_slice =
      grpc_slice_from_copied_buffer(status_details, status_details_len);
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
    ctx->send_message =
        grpcsharp_create_byte_buffer_from_stolen_slices(optional_send_buffer);
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
  grpc_call_error ret = grpcsharp_call_start_batch(call, ops, nops, ctx, NULL);
  grpc_slice_unref(status_details_slice);
  return ret;
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_recv_message(grpc_call* call, grpcsharp_batch_context* ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_MESSAGE;
  ops[0].data.recv_message.recv_message = &(ctx->recv_message);
  ops[0].flags = 0;
  ops[0].reserved = NULL;
  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_serverside(grpc_call* call, grpcsharp_batch_context* ctx) {
  /* TODO: don't use magic number */
  grpc_op ops[1];
  ops[0].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  ops[0].data.recv_close_on_server.cancelled =
      (&ctx->recv_close_on_server_cancelled);
  ops[0].flags = 0;
  ops[0].reserved = NULL;

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_send_initial_metadata(
    grpc_call* call, grpcsharp_batch_context* ctx,
    grpc_metadata_array* initial_metadata) {
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

  return grpcsharp_call_start_batch(call, ops, sizeof(ops) / sizeof(ops[0]),
                                    ctx, NULL);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_set_credentials(grpc_call* call, grpc_call_credentials* creds) {
  return grpc_call_set_credentials(call, creds);
}

/* Server */

GPR_EXPORT grpc_server* GPR_CALLTYPE
grpcsharp_server_create(const grpc_channel_args* args) {
  return grpc_server_create(args, NULL);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_register_completion_queue(
    grpc_server* server, grpc_completion_queue* cq) {
  grpc_server_register_completion_queue(server, cq, NULL);
}

GPR_EXPORT int32_t GPR_CALLTYPE grpcsharp_server_add_insecure_http2_port(
    grpc_server* server, const char* addr) {
  return grpc_server_add_insecure_http2_port(server, addr);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_start(grpc_server* server) {
  grpc_server_start(server);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_shutdown_and_notify_callback(
    grpc_server* server, grpc_completion_queue* cq,
    grpcsharp_batch_context* ctx) {
  grpc_server_shutdown_and_notify(server, cq, ctx);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_server_cancel_all_calls(grpc_server* server) {
  grpc_server_cancel_all_calls(server);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_destroy(grpc_server* server) {
  grpc_server_destroy(server);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_server_request_call(grpc_server* server, grpc_completion_queue* cq,
                              grpcsharp_request_call_context* ctx) {
  return grpc_server_request_call(server, &(ctx->call), &(ctx->call_details),
                                  &(ctx->request_metadata), cq, cq, ctx);
}

/* Native callback dispatcher */

typedef int(GPR_CALLTYPE* grpcsharp_native_callback_dispatcher_func)(
    void* tag, void* arg0, void* arg1, void* arg2, void* arg3, void* arg4,
    void* arg5);

static grpcsharp_native_callback_dispatcher_func native_callback_dispatcher =
    NULL;

GPR_EXPORT void GPR_CALLTYPE grpcsharp_native_callback_dispatcher_init(
    grpcsharp_native_callback_dispatcher_func func) {
  GPR_ASSERT(func);
  native_callback_dispatcher = func;
}

/* Security */

static char* default_pem_root_certs = NULL;

static grpc_ssl_roots_override_result override_ssl_roots_handler(
    char** pem_root_certs) {
  if (!default_pem_root_certs) {
    *pem_root_certs = NULL;
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY;
  }
  *pem_root_certs = gpr_strdup(default_pem_root_certs);
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_override_default_ssl_roots(const char* pem_root_certs) {
  /*
   * This currently wastes ~300kB of memory by keeping a copy of roots
   * in a static variable, but for desktop/server use, the overhead
   * is negligible. In the future, we might want to change the behavior
   * for mobile (e.g. Xamarin).
   */
  default_pem_root_certs = gpr_strdup(pem_root_certs);
  grpc_set_ssl_roots_override_callback(override_ssl_roots_handler);
}

static void grpcsharp_verify_peer_destroy_handler(void* userdata) {
  native_callback_dispatcher(userdata, NULL, NULL, (void*)1, NULL, NULL, NULL);
}

static int grpcsharp_verify_peer_handler(const char* target_name,
                                         const char* peer_pem, void* userdata) {
  return native_callback_dispatcher(userdata, (void*)target_name,
                                    (void*)peer_pem, (void*)0, NULL, NULL,
                                    NULL);
}

GPR_EXPORT grpc_channel_credentials* GPR_CALLTYPE
grpcsharp_ssl_credentials_create(const char* pem_root_certs,
                                 const char* key_cert_pair_cert_chain,
                                 const char* key_cert_pair_private_key,
                                 void* verify_peer_callback_tag) {
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  verify_peer_options verify_options;
  grpc_ssl_pem_key_cert_pair* key_cert_pair_ptr = NULL;
  verify_peer_options* verify_options_ptr = NULL;

  if (key_cert_pair_cert_chain || key_cert_pair_private_key) {
    memset(&key_cert_pair, 0, sizeof(key_cert_pair));
    key_cert_pair.cert_chain = key_cert_pair_cert_chain;
    key_cert_pair.private_key = key_cert_pair_private_key;
    key_cert_pair_ptr = &key_cert_pair;
  } else {
    GPR_ASSERT(!key_cert_pair_cert_chain);
    GPR_ASSERT(!key_cert_pair_private_key);
  }

  if (verify_peer_callback_tag != NULL) {
    memset(&verify_options, 0, sizeof(verify_peer_options));
    verify_options.verify_peer_callback_userdata = verify_peer_callback_tag;
    verify_options.verify_peer_destruct = grpcsharp_verify_peer_destroy_handler;
    verify_options.verify_peer_callback = grpcsharp_verify_peer_handler;
    verify_options_ptr = &verify_options;
  }

  return grpc_ssl_credentials_create(pem_root_certs, key_cert_pair_ptr,
                                     verify_options_ptr, NULL);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_channel_credentials_release(grpc_channel_credentials* creds) {
  grpc_channel_credentials_release(creds);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_call_credentials_release(grpc_call_credentials* creds) {
  grpc_call_credentials_release(creds);
}

GPR_EXPORT grpc_channel* GPR_CALLTYPE grpcsharp_secure_channel_create(
    grpc_channel_credentials* creds, const char* target,
    const grpc_channel_args* args) {
  return grpc_secure_channel_create(creds, target, args, NULL);
}

GPR_EXPORT grpc_server_credentials* GPR_CALLTYPE
grpcsharp_ssl_server_credentials_create(
    const char* pem_root_certs, const char** key_cert_pair_cert_chain_array,
    const char** key_cert_pair_private_key_array, size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_request_type) {
  size_t i;
  grpc_server_credentials* creds;
  grpc_ssl_pem_key_cert_pair* key_cert_pairs =
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
  creds = grpc_ssl_server_credentials_create_ex(pem_root_certs, key_cert_pairs,
                                                num_key_cert_pairs,
                                                client_request_type, NULL);
  gpr_free(key_cert_pairs);
  return creds;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_server_credentials_release(grpc_server_credentials* creds) {
  grpc_server_credentials_release(creds);
}

GPR_EXPORT int32_t GPR_CALLTYPE grpcsharp_server_add_secure_http2_port(
    grpc_server* server, const char* addr, grpc_server_credentials* creds) {
  return grpc_server_add_secure_http2_port(server, addr, creds);
}

GPR_EXPORT grpc_channel_credentials* GPR_CALLTYPE
grpcsharp_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* call_creds) {
  return grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                   NULL);
}

GPR_EXPORT grpc_call_credentials* GPR_CALLTYPE
grpcsharp_composite_call_credentials_create(grpc_call_credentials* creds1,
                                            grpc_call_credentials* creds2) {
  return grpc_composite_call_credentials_create(creds1, creds2, NULL);
}

/* Metadata credentials plugin */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_metadata_credentials_notify_from_plugin(
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata_array* metadata, grpc_status_code status,
    const char* error_details) {
  if (metadata) {
    cb(user_data, metadata->metadata, metadata->count, status, error_details);
  } else {
    cb(user_data, NULL, 0, status, error_details);
  }
}

static int grpcsharp_get_metadata_handler(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* status,
    const char** error_details) {
  (void)creds_md;
  (void)num_creds_md;
  (void)status;
  (void)error_details;
  // the "context" object and its contents are only guaranteed to live until
  // this handler returns (which could result in use-after-free for async
  // handling of the callback), so the C# counterpart of this handler
  // must make a copy of the "service_url" and "method_name" strings before
  // it returns if it wants to uses these strings.
  native_callback_dispatcher(state, (void*)context.service_url,
                             (void*)context.method_name, cb, user_data,
                             (void*)0, NULL);
  return 0; /* Asynchronous return. */
}

static void grpcsharp_metadata_credentials_destroy_handler(void* state) {
  native_callback_dispatcher(state, NULL, NULL, NULL, NULL, (void*)1, NULL);
}

GPR_EXPORT grpc_call_credentials* GPR_CALLTYPE
grpcsharp_metadata_credentials_create_from_plugin(void* callback_tag) {
  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = grpcsharp_get_metadata_handler;
  plugin.destroy = grpcsharp_metadata_credentials_destroy_handler;
  plugin.state = callback_tag;
  plugin.type = "";
  // TODO(yihuazhang): Expose min_security_level via the C# API so
  // that applications can decide what minimum security level their
  // plugins require.
  return grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, NULL);
}

/* Auth context */

GPR_EXPORT grpc_auth_context* GPR_CALLTYPE
grpcsharp_call_auth_context(grpc_call* call) {
  return grpc_call_auth_context(call);
}

GPR_EXPORT const char* GPR_CALLTYPE
grpcsharp_auth_context_peer_identity_property_name(
    const grpc_auth_context* ctx) {
  return grpc_auth_context_peer_identity_property_name(ctx);
}

GPR_EXPORT grpc_auth_property_iterator GPR_CALLTYPE
grpcsharp_auth_context_property_iterator(const grpc_auth_context* ctx) {
  return grpc_auth_context_property_iterator(ctx);
}

GPR_EXPORT const grpc_auth_property* GPR_CALLTYPE
grpcsharp_auth_property_iterator_next(grpc_auth_property_iterator* it) {
  return grpc_auth_property_iterator_next(it);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_auth_context_release(grpc_auth_context* ctx) {
  grpc_auth_context_release(ctx);
}

/* Logging */

typedef void(GPR_CALLTYPE* grpcsharp_log_func)(const char* file, int32_t line,
                                               uint64_t thd_id,
                                               const char* severity_string,
                                               const char* msg);
static grpcsharp_log_func log_func = NULL;

/* Redirects gpr_log to log_func callback */
static void grpcsharp_log_handler(gpr_log_func_args* args) {
  log_func(args->file, args->line, gpr_thd_currentid(),
           gpr_log_severity_string(args->severity), args->message);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_redirect_log(grpcsharp_log_func func) {
  GPR_ASSERT(func);
  log_func = func;
  gpr_set_log_function(grpcsharp_log_handler);
}

typedef void(GPR_CALLTYPE* test_callback_funcptr)(int32_t success);

/* Slice buffer functionality */
GPR_EXPORT grpc_slice_buffer* GPR_CALLTYPE grpcsharp_slice_buffer_create() {
  grpc_slice_buffer* slice_buffer =
      (grpc_slice_buffer*)gpr_malloc(sizeof(grpc_slice_buffer));
  grpc_slice_buffer_init(slice_buffer);
  return slice_buffer;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_slice_buffer_reset_and_unref(grpc_slice_buffer* buffer) {
  grpc_slice_buffer_reset_and_unref(buffer);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_slice_buffer_destroy(grpc_slice_buffer* buffer) {
  grpc_slice_buffer_destroy(buffer);
  gpr_free(buffer);
}

GPR_EXPORT size_t GPR_CALLTYPE
grpcsharp_slice_buffer_slice_count(grpc_slice_buffer* buffer) {
  return buffer->count;
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_slice_buffer_slice_peek(grpc_slice_buffer* buffer, size_t index,
                                  size_t* slice_len, uint8_t** slice_data_ptr) {
  GPR_ASSERT(buffer->count > index);
  grpc_slice* slice_ptr = &buffer->slices[index];
  *slice_len = GRPC_SLICE_LENGTH(*slice_ptr);
  *slice_data_ptr = GRPC_SLICE_START_PTR(*slice_ptr);
}

GPR_EXPORT void* GPR_CALLTYPE grpcsharp_slice_buffer_adjust_tail_space(
    grpc_slice_buffer* buffer, size_t available_tail_space,
    size_t requested_tail_space) {
  if (available_tail_space == requested_tail_space) {
    // nothing to do
  } else if (available_tail_space >= requested_tail_space) {
    grpc_slice_buffer_trim_end(
        buffer, available_tail_space - requested_tail_space, NULL);
  } else {
    if (available_tail_space > 0) {
      grpc_slice_buffer_trim_end(buffer, available_tail_space, NULL);
    }

    grpc_slice new_slice = grpc_slice_malloc(requested_tail_space);
    // grpc_slice_buffer_add_indexed always adds as a new slice entry into the
    // sb (which is suboptimal in some cases) but it doesn't have the problem of
    // sometimes splitting the continguous new_slice across two different slices
    // (like grpc_slice_buffer_add would)
    grpc_slice_buffer_add_indexed(buffer, new_slice);
  }

  if (buffer->count == 0) {
    return NULL;
  }
  grpc_slice* last_slice = &(buffer->slices[buffer->count - 1]);
  return GRPC_SLICE_END_PTR(*last_slice) - requested_tail_space;
}

/* Version info */
GPR_EXPORT const char* GPR_CALLTYPE grpcsharp_version_string() {
  return grpc_version_string();
}

/* For testing */
GPR_EXPORT void GPR_CALLTYPE
grpcsharp_test_callback(test_callback_funcptr callback) {
  callback(1);
}

/* For testing */
GPR_EXPORT void* GPR_CALLTYPE grpcsharp_test_nop(void* ptr) { return ptr; }

/* For testing */
GPR_EXPORT int32_t GPR_CALLTYPE grpcsharp_sizeof_grpc_event(void) {
  return sizeof(grpc_event);
}

/* Override a method for testing */
GPR_EXPORT void GPR_CALLTYPE
grpcsharp_test_override_method(const char* method_name, const char* variant) {
  if (strcmp("grpcsharp_call_start_batch", method_name) == 0) {
    if (strcmp("nop", variant) == 0) {
      g_call_start_batch_func = grpcsharp_call_start_batch_nop;
    } else {
      GPR_ASSERT(0);
    }
  } else {
    GPR_ASSERT(0);
  }
}
