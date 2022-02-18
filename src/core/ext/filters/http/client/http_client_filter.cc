/*
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport_impl.h"

#define EXPECTED_CONTENT_TYPE "application/grpc"
#define EXPECTED_CONTENT_TYPE_LENGTH (sizeof(EXPECTED_CONTENT_TYPE) - 1)

static void recv_initial_metadata_ready(void* user_data,
                                        grpc_error_handle error);
static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle error);

namespace {
struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner(args.call_combiner) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready,
                      ::recv_initial_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      ::recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
  }

  ~call_data() { GRPC_ERROR_UNREF(recv_initial_metadata_error); }

  grpc_core::CallCombiner* call_combiner;
  // State for handling recv_initial_metadata ops.
  grpc_metadata_batch* recv_initial_metadata;
  grpc_error_handle recv_initial_metadata_error = GRPC_ERROR_NONE;
  grpc_closure* original_recv_initial_metadata_ready = nullptr;
  grpc_closure recv_initial_metadata_ready;
  // State for handling recv_trailing_metadata ops.
  grpc_metadata_batch* recv_trailing_metadata;
  grpc_closure* original_recv_trailing_metadata_ready;
  grpc_closure recv_trailing_metadata_ready;
  grpc_error_handle recv_trailing_metadata_error = GRPC_ERROR_NONE;
  bool seen_recv_trailing_metadata_ready = false;
};

struct channel_data {
  grpc_core::HttpSchemeMetadata::ValueType static_scheme;
  grpc_core::Slice user_agent;
};
}  // namespace

static grpc_error_handle client_filter_incoming_metadata(
    grpc_metadata_batch* b) {
  if (auto* status = b->get_pointer(grpc_core::HttpStatusMetadata())) {
    /* If both gRPC status and HTTP status are provided in the response, we
     * should prefer the gRPC status code, as mentioned in
     * https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
     */
    const grpc_status_code* grpc_status =
        b->get_pointer(grpc_core::GrpcStatusMetadata());
    if (grpc_status != nullptr || *status == 200) {
      b->Remove(grpc_core::HttpStatusMetadata());
    } else {
      std::string msg =
          absl::StrCat("Received http2 header with status: ", *status);
      grpc_error_handle e = grpc_error_set_str(
          grpc_error_set_int(
              grpc_error_set_str(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "Received http2 :status header with non-200 OK status"),
                  GRPC_ERROR_STR_VALUE, std::to_string(*status)),
              GRPC_ERROR_INT_GRPC_STATUS,
              grpc_http2_status_to_grpc_status(*status)),
          GRPC_ERROR_STR_GRPC_MESSAGE, msg);
      return e;
    }
  }

  if (grpc_core::Slice* grpc_message =
          b->get_pointer(grpc_core::GrpcMessageMetadata())) {
    *grpc_message =
        grpc_core::PermissivePercentDecodeSlice(std::move(*grpc_message));
  }

  b->Remove(grpc_core::ContentTypeMetadata());

  return GRPC_ERROR_NONE;
}

static void recv_initial_metadata_ready(void* user_data,
                                        grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    error = client_filter_incoming_metadata(calld->recv_initial_metadata);
    calld->recv_initial_metadata_error = GRPC_ERROR_REF(error);
  } else {
    (void)GRPC_ERROR_REF(error);
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready;
  calld->original_recv_initial_metadata_ready = nullptr;
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(
        calld->call_combiner, &calld->recv_trailing_metadata_ready,
        calld->recv_trailing_metadata_error, "continue recv_trailing_metadata");
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
}

static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready != nullptr) {
    calld->recv_trailing_metadata_error = GRPC_ERROR_REF(error);
    calld->seen_recv_trailing_metadata_ready = true;
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring recv_trailing_metadata_ready until "
                            "after recv_initial_metadata_ready");
    return;
  }
  if (error == GRPC_ERROR_NONE) {
    error = client_filter_incoming_metadata(calld->recv_trailing_metadata);
  } else {
    (void)GRPC_ERROR_REF(error);
  }
  error = grpc_error_add_child(
      error, GRPC_ERROR_REF(calld->recv_initial_metadata_error));
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_trailing_metadata_ready, error);
}

static void http_client_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* channeld = static_cast<channel_data*>(elem->channel_data);
  GPR_TIMER_SCOPE("http_client_start_transport_stream_op_batch", 0);

  if (batch->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_initial_metadata =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }

  if (batch->recv_trailing_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_trailing_metadata =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    calld->original_recv_trailing_metadata_ready =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }

  if (batch->send_initial_metadata) {
    /* Send : prefixed headers, which have to be before any application
       layer headers. */
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::HttpMethodMetadata(), grpc_core::HttpMethodMetadata::kPost);
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::HttpSchemeMetadata(), channeld->static_scheme);
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::TeMetadata(), grpc_core::TeMetadata::kTrailers);
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::ContentTypeMetadata(),
        grpc_core::ContentTypeMetadata::kApplicationGrpc);
    batch->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::UserAgentMetadata(), channeld->user_agent.Ref());
  }

  grpc_call_next_op(elem, batch);
}

/* Constructor for call_data */
static grpc_error_handle http_client_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void http_client_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

static grpc_core::HttpSchemeMetadata::ValueType scheme_from_args(
    const grpc_channel_args* args) {
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; ++i) {
      if (args->args[i].type == GRPC_ARG_STRING &&
          0 == strcmp(args->args[i].key, GRPC_ARG_HTTP2_SCHEME)) {
        grpc_core::HttpSchemeMetadata::ValueType scheme =
            grpc_core::HttpSchemeMetadata::Parse(
                args->args[i].value.string,
                [](absl::string_view, const grpc_core::Slice&) {});
        if (scheme != grpc_core::HttpSchemeMetadata::kInvalid) return scheme;
      }
    }
  }
  return grpc_core::HttpSchemeMetadata::kHttp;
}

static grpc_core::Slice user_agent_from_args(const grpc_channel_args* args,
                                             const char* transport_name) {
  std::vector<std::string> user_agent_fields;

  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_PRIMARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_PRIMARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }

  user_agent_fields.push_back(
      absl::StrFormat("grpc-c/%s (%s; %s)", grpc_version_string(),
                      GPR_PLATFORM_STRING, transport_name));

  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_SECONDARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_SECONDARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }

  std::string user_agent_string = absl::StrJoin(user_agent_fields, " ");
  return grpc_core::Slice::FromCopiedString(user_agent_string.c_str());
}

/* Constructor for channel_data */
static grpc_error_handle http_client_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  new (chand) channel_data();
  GPR_ASSERT(!args->is_last);
  auto* transport = grpc_channel_args_find_pointer<grpc_transport>(
      args->channel_args, GRPC_ARG_TRANSPORT);
  GPR_ASSERT(transport != nullptr);
  chand->static_scheme = scheme_from_args(args->channel_args);
  chand->user_agent = grpc_core::Slice(
      user_agent_from_args(args->channel_args, transport->vtable->name));
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void http_client_destroy_channel_elem(grpc_channel_element* elem) {
  static_cast<channel_data*>(elem->channel_data)->~channel_data();
}

const grpc_channel_filter grpc_http_client_filter = {
    http_client_start_transport_stream_op_batch,
    nullptr,
    grpc_channel_next_op,
    sizeof(call_data),
    http_client_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    http_client_destroy_call_elem,
    sizeof(channel_data),
    http_client_init_channel_elem,
    http_client_destroy_channel_elem,
    grpc_channel_next_get_info,
    "http-client"};
