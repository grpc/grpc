//
// Copyright 2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "src/core/lib/channel/message_size_filter.h"

#include <limits.h>
#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/service_config.h"

typedef struct message_size_limits {
  int max_send_size;
  int max_recv_size;
} message_size_limits;

static void* message_size_limits_copy(void* value) {
  void* new_value = gpr_malloc(sizeof(message_size_limits));
  memcpy(new_value, value, sizeof(message_size_limits));
  return new_value;
}

static void message_size_limits_free(grpc_exec_ctx* exec_ctx, void* value) {
  gpr_free(value);
}

static const grpc_slice_hash_table_vtable message_size_limits_vtable = {
    message_size_limits_free, message_size_limits_copy};

static void* message_size_limits_create_from_json(const grpc_json* json) {
  int max_request_message_bytes = -1;
  int max_response_message_bytes = -1;
  for (grpc_json* field = json->child; field != NULL; field = field->next) {
    if (field->key == NULL) continue;
    if (strcmp(field->key, "maxRequestMessageBytes") == 0) {
      if (max_request_message_bytes >= 0) return NULL;  // Duplicate.
      if (field->type != GRPC_JSON_STRING && field->type != GRPC_JSON_NUMBER) {
        return NULL;
      }
      max_request_message_bytes = gpr_parse_nonnegative_int(field->value);
      if (max_request_message_bytes == -1) return NULL;
    } else if (strcmp(field->key, "maxResponseMessageBytes") == 0) {
      if (max_response_message_bytes >= 0) return NULL;  // Duplicate.
      if (field->type != GRPC_JSON_STRING && field->type != GRPC_JSON_NUMBER) {
        return NULL;
      }
      max_response_message_bytes = gpr_parse_nonnegative_int(field->value);
      if (max_response_message_bytes == -1) return NULL;
    }
  }
  message_size_limits* value = gpr_malloc(sizeof(message_size_limits));
  value->max_send_size = max_request_message_bytes;
  value->max_recv_size = max_response_message_bytes;
  return value;
}

typedef struct call_data {
  int max_send_size;
  int max_recv_size;
  // Receive closures are chained: we inject this closure as the
  // recv_message_ready up-call on transport_stream_op, and remember to
  // call our next_recv_message_ready member after handling it.
  grpc_closure recv_message_ready;
  // Used by recv_message_ready.
  grpc_byte_stream** recv_message;
  // Original recv_message_ready callback, invoked after our own.
  grpc_closure* next_recv_message_ready;
} call_data;

typedef struct channel_data {
  int max_send_size;
  int max_recv_size;
  // Maps path names to message_size_limits structs.
  grpc_slice_hash_table* method_limit_table;
} channel_data;

// Callback invoked when we receive a message.  Here we check the max
// receive message size.
static void recv_message_ready(grpc_exec_ctx* exec_ctx, void* user_data,
                               grpc_error* error) {
  grpc_call_element* elem = user_data;
  call_data* calld = elem->call_data;
  if (*calld->recv_message != NULL && calld->max_recv_size >= 0 &&
      (*calld->recv_message)->length > (size_t)calld->max_recv_size) {
    char* message_string;
    gpr_asprintf(&message_string,
                 "Received message larger than max (%u vs. %d)",
                 (*calld->recv_message)->length, calld->max_recv_size);
    grpc_error* new_error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(message_string),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED);
    if (error == GRPC_ERROR_NONE) {
      error = new_error;
    } else {
      error = grpc_error_add_child(error, new_error);
      GRPC_ERROR_UNREF(new_error);
    }
    gpr_free(message_string);
  }
  // Invoke the next callback.
  grpc_closure_run(exec_ctx, calld->next_recv_message_ready, error);
}

// Start transport stream op.
static void start_transport_stream_op_batch(
    grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
    grpc_transport_stream_op_batch* op) {
  call_data* calld = elem->call_data;
  // Check max send message size.
  if (op->send_message && calld->max_send_size >= 0 &&
      op->payload->send_message.send_message->length >
          (size_t)calld->max_send_size) {
    char* message_string;
    gpr_asprintf(&message_string, "Sent message larger than max (%u vs. %d)",
                 op->payload->send_message.send_message->length,
                 calld->max_send_size);
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, op,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(message_string),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_RESOURCE_EXHAUSTED));
    gpr_free(message_string);
    return;
  }
  // Inject callback for receiving a message.
  if (op->recv_message) {
    calld->next_recv_message_ready =
        op->payload->recv_message.recv_message_ready;
    calld->recv_message = op->payload->recv_message.recv_message;
    op->payload->recv_message.recv_message_ready = &calld->recv_message_ready;
  }
  // Chain to the next filter.
  grpc_call_next_op(exec_ctx, elem, op);
}

// Constructor for call_data.
static grpc_error* init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  channel_data* chand = elem->channel_data;
  call_data* calld = elem->call_data;
  calld->next_recv_message_ready = NULL;
  grpc_closure_init(&calld->recv_message_ready, recv_message_ready, elem,
                    grpc_schedule_on_exec_ctx);
  // Get max sizes from channel data, then merge in per-method config values.
  // Note: Per-method config is only available on the client, so we
  // apply the max request size to the send limit and the max response
  // size to the receive limit.
  calld->max_send_size = chand->max_send_size;
  calld->max_recv_size = chand->max_recv_size;
  if (chand->method_limit_table != NULL) {
    message_size_limits* limits = grpc_method_config_table_get(
        exec_ctx, chand->method_limit_table, args->path);
    if (limits != NULL) {
      if (limits->max_send_size >= 0 &&
          (limits->max_send_size < calld->max_send_size ||
           calld->max_send_size < 0)) {
        calld->max_send_size = limits->max_send_size;
      }
      if (limits->max_recv_size >= 0 &&
          (limits->max_recv_size < calld->max_recv_size ||
           calld->max_recv_size < 0)) {
        calld->max_recv_size = limits->max_recv_size;
      }
    }
  }
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {}

// Constructor for channel_data.
static grpc_error* init_channel_elem(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  channel_data* chand = elem->channel_data;
  chand->max_send_size = GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH;
  chand->max_recv_size = GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH;
  for (size_t i = 0; i < args->channel_args->num_args; ++i) {
    if (strcmp(args->channel_args->args[i].key,
               GRPC_ARG_MAX_SEND_MESSAGE_LENGTH) == 0) {
      const grpc_integer_options options = {
          GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH, -1, INT_MAX};
      chand->max_send_size =
          grpc_channel_arg_get_integer(&args->channel_args->args[i], options);
    }
    if (strcmp(args->channel_args->args[i].key,
               GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH) == 0) {
      const grpc_integer_options options = {
          GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH, -1, INT_MAX};
      chand->max_recv_size =
          grpc_channel_arg_get_integer(&args->channel_args->args[i], options);
    }
  }
  // Get method config table from channel args.
  const grpc_arg* channel_arg =
      grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVICE_CONFIG);
  if (channel_arg != NULL) {
    GPR_ASSERT(channel_arg->type == GRPC_ARG_STRING);
    grpc_service_config* service_config =
        grpc_service_config_create(channel_arg->value.string);
    if (service_config != NULL) {
      chand->method_limit_table =
          grpc_service_config_create_method_config_table(
              exec_ctx, service_config, message_size_limits_create_from_json,
              &message_size_limits_vtable);
      grpc_service_config_destroy(service_config);
    }
  }
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {
  channel_data* chand = elem->channel_data;
  grpc_slice_hash_table_unref(exec_ctx, chand->method_limit_table);
}

const grpc_channel_filter grpc_message_size_filter = {
    start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "message_size"};
