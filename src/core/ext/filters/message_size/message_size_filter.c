//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <limits.h>
#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/service_config.h"

typedef struct message_size_limits {
  int max_send_size;
  int max_recv_size;
} message_size_limits;

static void message_size_limits_free(grpc_exec_ctx* exec_ctx, void* value) {
  gpr_free(value);
}

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
  message_size_limits* value =
      (message_size_limits*)gpr_malloc(sizeof(message_size_limits));
  value->max_send_size = max_request_message_bytes;
  value->max_recv_size = max_response_message_bytes;
  return value;
}

typedef struct call_data {
  grpc_call_combiner* call_combiner;
  message_size_limits limits;
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
  message_size_limits limits;
  // Maps path names to message_size_limits structs.
  grpc_slice_hash_table* method_limit_table;
} channel_data;

// Callback invoked when we receive a message.  Here we check the max
// receive message size.
static void recv_message_ready(grpc_exec_ctx* exec_ctx, void* user_data,
                               grpc_error* error) {
  grpc_call_element* elem = (grpc_call_element*)user_data;
  call_data* calld = (call_data*)elem->call_data;
  if (*calld->recv_message != NULL && calld->limits.max_recv_size >= 0 &&
      (*calld->recv_message)->length > (size_t)calld->limits.max_recv_size) {
    char* message_string;
    gpr_asprintf(&message_string,
                 "Received message larger than max (%u vs. %d)",
                 (*calld->recv_message)->length, calld->limits.max_recv_size);
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
  } else {
    GRPC_ERROR_REF(error);
  }
  // Invoke the next callback.
  GRPC_CLOSURE_RUN(exec_ctx, calld->next_recv_message_ready, error);
}

// Start transport stream op.
static void start_transport_stream_op_batch(
    grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
    grpc_transport_stream_op_batch* op) {
  call_data* calld = (call_data*)elem->call_data;
  // Check max send message size.
  if (op->send_message && calld->limits.max_send_size >= 0 &&
      op->payload->send_message.send_message->length >
          (size_t)calld->limits.max_send_size) {
    char* message_string;
    gpr_asprintf(&message_string, "Sent message larger than max (%u vs. %d)",
                 op->payload->send_message.send_message->length,
                 calld->limits.max_send_size);
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, op,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(message_string),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_RESOURCE_EXHAUSTED),
        calld->call_combiner);
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
  channel_data* chand = (channel_data*)elem->channel_data;
  call_data* calld = (call_data*)elem->call_data;
  calld->call_combiner = args->call_combiner;
  calld->next_recv_message_ready = NULL;
  GRPC_CLOSURE_INIT(&calld->recv_message_ready, recv_message_ready, elem,
                    grpc_schedule_on_exec_ctx);
  // Get max sizes from channel data, then merge in per-method config values.
  // Note: Per-method config is only available on the client, so we
  // apply the max request size to the send limit and the max response
  // size to the receive limit.
  calld->limits = chand->limits;
  if (chand->method_limit_table != NULL) {
    message_size_limits* limits =
        (message_size_limits*)grpc_method_config_table_get(
            exec_ctx, chand->method_limit_table, args->path);
    if (limits != NULL) {
      if (limits->max_send_size >= 0 &&
          (limits->max_send_size < calld->limits.max_send_size ||
           calld->limits.max_send_size < 0)) {
        calld->limits.max_send_size = limits->max_send_size;
      }
      if (limits->max_recv_size >= 0 &&
          (limits->max_recv_size < calld->limits.max_recv_size ||
           calld->limits.max_recv_size < 0)) {
        calld->limits.max_recv_size = limits->max_recv_size;
      }
    }
  }
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {}

static int default_size(const grpc_channel_args* args,
                        int without_minimal_stack) {
  if (grpc_channel_args_want_minimal_stack(args)) {
    return -1;
  }
  return without_minimal_stack;
}

message_size_limits get_message_size_limits(
    const grpc_channel_args* channel_args) {
  message_size_limits lim;
  lim.max_send_size =
      default_size(channel_args, GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH);
  lim.max_recv_size =
      default_size(channel_args, GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH);
  for (size_t i = 0; i < channel_args->num_args; ++i) {
    if (strcmp(channel_args->args[i].key, GRPC_ARG_MAX_SEND_MESSAGE_LENGTH) ==
        0) {
      const grpc_integer_options options = {lim.max_send_size, -1, INT_MAX};
      lim.max_send_size =
          grpc_channel_arg_get_integer(&channel_args->args[i], options);
    }
    if (strcmp(channel_args->args[i].key,
               GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH) == 0) {
      const grpc_integer_options options = {lim.max_recv_size, -1, INT_MAX};
      lim.max_recv_size =
          grpc_channel_arg_get_integer(&channel_args->args[i], options);
    }
  }
  return lim;
}

// Constructor for channel_data.
static grpc_error* init_channel_elem(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  channel_data* chand = (channel_data*)elem->channel_data;
  chand->limits = get_message_size_limits(args->channel_args);
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
              message_size_limits_free);
      grpc_service_config_destroy(service_config);
    }
  }
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {
  channel_data* chand = (channel_data*)elem->channel_data;
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
    grpc_channel_next_get_info,
    "message_size"};

static bool maybe_add_message_size_filter(grpc_exec_ctx* exec_ctx,
                                          grpc_channel_stack_builder* builder,
                                          void* arg) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  bool enable = false;
  message_size_limits lim = get_message_size_limits(channel_args);
  if (lim.max_send_size != -1 || lim.max_recv_size != -1) {
    enable = true;
  }
  const grpc_arg* a =
      grpc_channel_args_find(channel_args, GRPC_ARG_SERVICE_CONFIG);
  if (a != NULL) {
    enable = true;
  }
  if (enable) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, &grpc_message_size_filter, NULL, NULL);
  } else {
    return true;
  }
}

void grpc_message_size_filter_init(void) {
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, NULL);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, NULL);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, NULL);
}

void grpc_message_size_filter_shutdown(void) {}
