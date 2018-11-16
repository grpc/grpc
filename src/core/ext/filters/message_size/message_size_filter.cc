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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <limits.h>
#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/service_config.h"

typedef struct {
  int max_send_size;
  int max_recv_size;
} message_size_limits;

namespace grpc_core {
namespace {

class MessageSizeLimits : public RefCounted<MessageSizeLimits> {
 public:
  static RefCountedPtr<MessageSizeLimits> CreateFromJson(const grpc_json* json);

  const message_size_limits& limits() const { return limits_; }

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* grpc_core::New(Args&&... args);

  MessageSizeLimits(int max_send_size, int max_recv_size) {
    limits_.max_send_size = max_send_size;
    limits_.max_recv_size = max_recv_size;
  }

  message_size_limits limits_;
};

RefCountedPtr<MessageSizeLimits> MessageSizeLimits::CreateFromJson(
    const grpc_json* json) {
  int max_request_message_bytes = -1;
  int max_response_message_bytes = -1;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "maxRequestMessageBytes") == 0) {
      if (max_request_message_bytes >= 0) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_STRING && field->type != GRPC_JSON_NUMBER) {
        return nullptr;
      }
      max_request_message_bytes = gpr_parse_nonnegative_int(field->value);
      if (max_request_message_bytes == -1) return nullptr;
    } else if (strcmp(field->key, "maxResponseMessageBytes") == 0) {
      if (max_response_message_bytes >= 0) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_STRING && field->type != GRPC_JSON_NUMBER) {
        return nullptr;
      }
      max_response_message_bytes = gpr_parse_nonnegative_int(field->value);
      if (max_response_message_bytes == -1) return nullptr;
    }
  }
  return MakeRefCounted<MessageSizeLimits>(max_request_message_bytes,
                                           max_response_message_bytes);
}

}  // namespace
}  // namespace grpc_core

static void recv_message_ready(void* user_data, grpc_error* error);
static void recv_trailing_metadata_ready(void* user_data, grpc_error* error);

namespace {

struct channel_data {
  message_size_limits limits;
  // Maps path names to refcounted_message_size_limits structs.
  grpc_core::RefCountedPtr<grpc_core::SliceHashTable<
      grpc_core::RefCountedPtr<grpc_core::MessageSizeLimits>>>
      method_limit_table;
};

struct call_data {
  call_data(grpc_call_element* elem, const channel_data& chand,
            const grpc_call_element_args& args)
      : call_combiner(args.call_combiner), limits(chand.limits) {
    GRPC_CLOSURE_INIT(&recv_message_ready, ::recv_message_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      ::recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    // Get max sizes from channel data, then merge in per-method config values.
    // Note: Per-method config is only available on the client, so we
    // apply the max request size to the send limit and the max response
    // size to the receive limit.
    if (chand.method_limit_table != nullptr) {
      grpc_core::RefCountedPtr<grpc_core::MessageSizeLimits> limits =
          grpc_core::ServiceConfig::MethodConfigTableLookup(
              *chand.method_limit_table, args.path);
      if (limits != nullptr) {
        if (limits->limits().max_send_size >= 0 &&
            (limits->limits().max_send_size < this->limits.max_send_size ||
             this->limits.max_send_size < 0)) {
          this->limits.max_send_size = limits->limits().max_send_size;
        }
        if (limits->limits().max_recv_size >= 0 &&
            (limits->limits().max_recv_size < this->limits.max_recv_size ||
             this->limits.max_recv_size < 0)) {
          this->limits.max_recv_size = limits->limits().max_recv_size;
        }
      }
    }
  }

  ~call_data() { GRPC_ERROR_UNREF(error); }

  grpc_call_combiner* call_combiner;
  message_size_limits limits;
  // Receive closures are chained: we inject this closure as the
  // recv_message_ready up-call on transport_stream_op, and remember to
  // call our next_recv_message_ready member after handling it.
  grpc_closure recv_message_ready;
  grpc_closure recv_trailing_metadata_ready;
  // The error caused by a message that is too large, or GRPC_ERROR_NONE
  grpc_error* error = GRPC_ERROR_NONE;
  // Used by recv_message_ready.
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message = nullptr;
  // Original recv_message_ready callback, invoked after our own.
  grpc_closure* next_recv_message_ready = nullptr;
  // Original recv_trailing_metadata callback, invoked after our own.
  grpc_closure* original_recv_trailing_metadata_ready;
  bool seen_recv_trailing_metadata = false;
  grpc_error* recv_trailing_metadata_error;
};

}  // namespace

// Callback invoked when we receive a message.  Here we check the max
// receive message size.
static void recv_message_ready(void* user_data, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (*calld->recv_message != nullptr && calld->limits.max_recv_size >= 0 &&
      (*calld->recv_message)->length() >
          static_cast<size_t>(calld->limits.max_recv_size)) {
    char* message_string;
    gpr_asprintf(&message_string,
                 "Received message larger than max (%u vs. %d)",
                 (*calld->recv_message)->length(), calld->limits.max_recv_size);
    grpc_error* new_error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(message_string),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED);
    GRPC_ERROR_UNREF(calld->error);
    if (error == GRPC_ERROR_NONE) {
      error = new_error;
    } else {
      error = grpc_error_add_child(error, new_error);
    }
    calld->error = GRPC_ERROR_REF(error);
    gpr_free(message_string);
  } else {
    GRPC_ERROR_REF(error);
  }
  // Invoke the next callback.
  grpc_closure* closure = calld->next_recv_message_ready;
  calld->next_recv_message_ready = nullptr;
  if (calld->seen_recv_trailing_metadata) {
    /* We might potentially see another RECV_MESSAGE op. In that case, we do not
     * want to run the recv_trailing_metadata_ready closure again. The newer
     * RECV_MESSAGE op cannot cause any errors since the transport has already
     * invoked the recv_trailing_metadata_ready closure and all further
     * RECV_MESSAGE ops will get null payloads. */
    calld->seen_recv_trailing_metadata = false;
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_error,
                             "continue recv_trailing_metadata_ready");
  }
  GRPC_CLOSURE_RUN(closure, error);
}

// Callback invoked on completion of recv_trailing_metadata
// Notifies the recv_trailing_metadata batch of any message size failures
static void recv_trailing_metadata_ready(void* user_data, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->next_recv_message_ready != nullptr) {
    calld->seen_recv_trailing_metadata = true;
    calld->recv_trailing_metadata_error = GRPC_ERROR_REF(error);
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring recv_trailing_metadata_ready until "
                            "after recv_message_ready");
    return;
  }
  error =
      grpc_error_add_child(GRPC_ERROR_REF(error), GRPC_ERROR_REF(calld->error));
  // Invoke the next callback.
  GRPC_CLOSURE_RUN(calld->original_recv_trailing_metadata_ready, error);
}

// Start transport stream op.
static void start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Check max send message size.
  if (op->send_message && calld->limits.max_send_size >= 0 &&
      op->payload->send_message.send_message->length() >
          static_cast<size_t>(calld->limits.max_send_size)) {
    char* message_string;
    gpr_asprintf(&message_string, "Sent message larger than max (%u vs. %d)",
                 op->payload->send_message.send_message->length(),
                 calld->limits.max_send_size);
    grpc_transport_stream_op_batch_finish_with_failure(
        op,
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
  // Inject callback for receiving trailing metadata.
  if (op->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, op);
}

// Constructor for call_data.
static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  new (elem->call_data) call_data(elem, *chand, *args);
  return GRPC_ERROR_NONE;
}

// Destructor for call_data.
static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  call_data* calld = (call_data*)elem->call_data;
  calld->~call_data();
}

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
static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->limits = get_message_size_limits(args->channel_args);
  // Get method config table from channel args.
  const grpc_arg* channel_arg =
      grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVICE_CONFIG);
  const char* service_config_str = grpc_channel_arg_get_string(channel_arg);
  if (service_config_str != nullptr) {
    grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config =
        grpc_core::ServiceConfig::Create(service_config_str);
    if (service_config != nullptr) {
      chand->method_limit_table = service_config->CreateMethodConfigTable(
          grpc_core::MessageSizeLimits::CreateFromJson);
    }
  }
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.
static void destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->method_limit_table.reset();
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

static bool maybe_add_message_size_filter(grpc_channel_stack_builder* builder,
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
  if (a != nullptr) {
    enable = true;
  }
  if (enable) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, &grpc_message_size_filter, nullptr, nullptr);
  } else {
    return true;
  }
}

void grpc_message_size_filter_init(void) {
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_message_size_filter, nullptr);
}

void grpc_message_size_filter_shutdown(void) {}
