/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client_authority_filter.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"

namespace {

struct call_data {
  grpc_core::CallCombiner* call_combiner;
};

struct channel_data {
  grpc_core::Slice default_authority;
};

void client_authority_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  // Handle send_initial_metadata.
  // If the initial metadata doesn't already contain :authority, add it.
  if (batch->send_initial_metadata &&
      batch->payload->send_initial_metadata.send_initial_metadata->get_pointer(
          grpc_core::HttpAuthorityMetadata()) == nullptr) {
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::HttpAuthorityMetadata(), chand->default_authority.Ref());
  }
  // Pass control down the stack.
  grpc_call_next_op(elem, batch);
}

/* Constructor for call_data */
grpc_error_handle client_authority_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->call_combiner = args->call_combiner;
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
void client_authority_destroy_call_elem(
    grpc_call_element* /*elem*/, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {}

/* Constructor for channel_data */
grpc_error_handle client_authority_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* chand = new (elem->channel_data) channel_data;
  const grpc_arg* default_authority_arg =
      grpc_channel_args_find(args->channel_args, GRPC_ARG_DEFAULT_AUTHORITY);
  if (default_authority_arg == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "GRPC_ARG_DEFAULT_AUTHORITY channel arg. not found. Note that direct "
        "channels must explicitly specify a value for this argument.");
  }
  const char* default_authority_str =
      grpc_channel_arg_get_string(default_authority_arg);
  if (default_authority_str == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "GRPC_ARG_DEFAULT_AUTHORITY channel arg. must be a string");
  }
  chand->default_authority =
      grpc_core::Slice::FromCopiedString(default_authority_str);
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
void client_authority_destroy_channel_elem(grpc_channel_element* elem) {
  static_cast<channel_data*>(elem->channel_data)->~channel_data();
}
}  // namespace

const grpc_channel_filter grpc_client_authority_filter = {
    client_authority_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    client_authority_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    client_authority_destroy_call_elem,
    sizeof(channel_data),
    client_authority_init_channel_elem,
    client_authority_destroy_channel_elem,
    grpc_channel_next_get_info,
    "authority"};

static bool add_client_authority_filter(grpc_channel_stack_builder* builder) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_arg* disable_client_authority_filter_arg = grpc_channel_args_find(
      channel_args, GRPC_ARG_DISABLE_CLIENT_AUTHORITY_FILTER);
  if (disable_client_authority_filter_arg != nullptr) {
    const bool is_client_authority_filter_disabled =
        grpc_channel_arg_get_bool(disable_client_authority_filter_arg, false);
    if (is_client_authority_filter_disabled) {
      return true;
    }
  }
  return grpc_channel_stack_builder_prepend_filter(
      builder, &grpc_client_authority_filter, nullptr, nullptr);
}

namespace grpc_core {
void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL, INT_MAX,
                                         add_client_authority_filter);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_DIRECT_CHANNEL, INT_MAX,
                                         add_client_authority_filter);
}
}  // namespace grpc_core
