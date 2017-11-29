//
// Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/workarounds/workaround_cronet_compression_filter.h"

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/workarounds/workaround_utils.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/metadata.h"

typedef struct call_data {
  // Receive closures are chained: we inject this closure as the
  // recv_initial_metadata_ready up-call on transport_stream_op, and remember to
  // call our next_recv_initial_metadata_ready member after handling it.
  grpc_closure recv_initial_metadata_ready;
  // Used by recv_initial_metadata_ready.
  grpc_metadata_batch* recv_initial_metadata;
  // Original recv_initial_metadata_ready callback, invoked after our own.
  grpc_closure* next_recv_initial_metadata_ready;

  // Marks whether the workaround is active
  bool workaround_active;
} call_data;

// Find the user agent metadata element in the batch
static bool get_user_agent_mdelem(const grpc_metadata_batch* batch,
                                  grpc_mdelem* md) {
  if (batch->idx.named.user_agent != nullptr) {
    *md = batch->idx.named.user_agent->md;
    return true;
  }
  return false;
}

// Callback invoked when we receive an initial metadata.
static void recv_initial_metadata_ready(grpc_exec_ctx* exec_ctx,
                                        void* user_data, grpc_error* error) {
  grpc_call_element* elem = (grpc_call_element*)user_data;
  call_data* calld = (call_data*)elem->call_data;

  if (GRPC_ERROR_NONE == error) {
    grpc_mdelem md;
    if (get_user_agent_mdelem(calld->recv_initial_metadata, &md)) {
      grpc_workaround_user_agent_md* user_agent_md = grpc_parse_user_agent(md);
      if (user_agent_md
              ->workaround_active[GRPC_WORKAROUND_ID_CRONET_COMPRESSION]) {
        calld->workaround_active = true;
      }
    }
  }

  // Invoke the next callback.
  GRPC_CLOSURE_RUN(exec_ctx, calld->next_recv_initial_metadata_ready,
                   GRPC_ERROR_REF(error));
}

// Start transport stream op.
static void start_transport_stream_op_batch(
    grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
    grpc_transport_stream_op_batch* op) {
  call_data* calld = (call_data*)elem->call_data;

  // Inject callback for receiving initial metadata
  if (op->recv_initial_metadata) {
    calld->next_recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
  }

  if (op->send_message) {
    /* Send message happens after client's user-agent (initial metadata) is
     * received, so workaround_active must be set already */
    if (calld->workaround_active) {
      op->payload->send_message.send_message->flags |= GRPC_WRITE_NO_COMPRESS;
    }
  }

  // Chain to the next filter.
  grpc_call_next_op(exec_ctx, elem, op);
}

// Constructor for call_data.
static grpc_error* init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  call_data* calld = (call_data*)elem->call_data;
  calld->next_recv_initial_metadata_ready = nullptr;
  calld->workaround_active = false;
  GRPC_CLOSURE_INIT(&calld->recv_initial_metadata_ready,
                    recv_initial_metadata_ready, elem,
                    grpc_schedule_on_exec_ctx);
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
  return GRPC_ERROR_NONE;
}

// Destructor for channel_data.
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {}

// Parse the user agent
static bool parse_user_agent(grpc_mdelem md) {
  const char grpc_objc_specifier[] = "grpc-objc/";
  const size_t grpc_objc_specifier_len = sizeof(grpc_objc_specifier) - 1;
  const char cronet_specifier[] = "cronet_http";
  const size_t cronet_specifier_len = sizeof(cronet_specifier) - 1;

  char* user_agent_str = grpc_slice_to_c_string(GRPC_MDVALUE(md));
  bool grpc_objc_specifier_seen = false;
  bool cronet_specifier_seen = false;
  char *major_version_str = user_agent_str, *minor_version_str;
  long major_version = 0, minor_version = 0;

  char* head = strtok(user_agent_str, " ");
  while (head != nullptr) {
    if (!grpc_objc_specifier_seen &&
        0 == strncmp(head, grpc_objc_specifier, grpc_objc_specifier_len)) {
      major_version_str = head + grpc_objc_specifier_len;
      grpc_objc_specifier_seen = true;
    } else if (grpc_objc_specifier_seen &&
               0 == strncmp(head, cronet_specifier, cronet_specifier_len)) {
      cronet_specifier_seen = true;
      break;
    }

    head = strtok(nullptr, " ");
  }
  if (grpc_objc_specifier_seen) {
    major_version_str = strtok(major_version_str, ".");
    minor_version_str = strtok(nullptr, ".");
    major_version = atol(major_version_str);
    minor_version = atol(minor_version_str);
  }

  gpr_free(user_agent_str);
  return (grpc_objc_specifier_seen && cronet_specifier_seen &&
          (major_version < 1 || (major_version == 1 && minor_version <= 3)));
}

const grpc_channel_filter grpc_workaround_cronet_compression_filter = {
    start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    0,
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "workaround_cronet_compression"};

static bool register_workaround_cronet_compression(
    grpc_exec_ctx* exec_ctx, grpc_channel_stack_builder* builder, void* arg) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_arg* a = grpc_channel_args_find(
      channel_args, GRPC_ARG_WORKAROUND_CRONET_COMPRESSION);
  if (a == nullptr) {
    return true;
  }
  if (grpc_channel_arg_get_bool(a, false) == false) {
    return true;
  }
  return grpc_channel_stack_builder_prepend_filter(
      builder, &grpc_workaround_cronet_compression_filter, nullptr, nullptr);
}

extern "C" void grpc_workaround_cronet_compression_filter_init(void) {
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_WORKAROUND_PRIORITY_HIGH,
      register_workaround_cronet_compression, nullptr);
  grpc_register_workaround(GRPC_WORKAROUND_ID_CRONET_COMPRESSION,
                           parse_user_agent);
}

extern "C" void grpc_workaround_cronet_compression_filter_shutdown(void) {}
