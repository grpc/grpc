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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

static void hs_recv_initial_metadata_ready(void* user_data,
                                           grpc_error_handle err);
static void hs_recv_trailing_metadata_ready(void* user_data,
                                            grpc_error_handle err);
static void hs_recv_message_ready(void* user_data, grpc_error_handle err);

namespace {

struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner(args.call_combiner) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready,
                      hs_recv_initial_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_message_ready, hs_recv_message_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      hs_recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
  }

  ~call_data() {
    GRPC_ERROR_UNREF(recv_initial_metadata_ready_error);
    if (have_read_stream) {
      read_stream->Orphan();
    }
  }

  grpc_core::CallCombiner* call_combiner;

  // If we see the recv_message contents in the GET query string, we
  // store it here.
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> read_stream;
  bool have_read_stream = false;

  // State for intercepting recv_initial_metadata.
  grpc_closure recv_initial_metadata_ready;
  grpc_error_handle recv_initial_metadata_ready_error = GRPC_ERROR_NONE;
  grpc_closure* original_recv_initial_metadata_ready;
  grpc_metadata_batch* recv_initial_metadata = nullptr;
  uint32_t* recv_initial_metadata_flags;
  bool seen_recv_initial_metadata_ready = false;

  // State for intercepting recv_message.
  grpc_closure* original_recv_message_ready;
  grpc_closure recv_message_ready;
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message;
  bool seen_recv_message_ready = false;

  // State for intercepting recv_trailing_metadata
  grpc_closure recv_trailing_metadata_ready;
  grpc_closure* original_recv_trailing_metadata_ready;
  grpc_error_handle recv_trailing_metadata_ready_error;
  bool seen_recv_trailing_metadata_ready = false;
};

struct channel_data {
  bool surface_user_agent;
};

}  // namespace

static grpc_error_handle hs_filter_outgoing_metadata(grpc_metadata_batch* b) {
  if (grpc_core::Slice* grpc_message =
          b->get_pointer(grpc_core::GrpcMessageMetadata())) {
    *grpc_message = grpc_core::PercentEncodeSlice(
        std::move(*grpc_message), grpc_core::PercentEncodingType::Compatible);
  }
  return GRPC_ERROR_NONE;
}

static void hs_add_error(const char* error_name, grpc_error_handle* cumulative,
                         grpc_error_handle new_err) {
  if (new_err == GRPC_ERROR_NONE) return;
  if (*cumulative == GRPC_ERROR_NONE) {
    *cumulative = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_name);
  }
  *cumulative = grpc_error_add_child(*cumulative, new_err);
}

static grpc_error_handle hs_filter_incoming_metadata(grpc_call_element* elem,
                                                     grpc_metadata_batch* b) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_error_handle error = GRPC_ERROR_NONE;
  static const char* error_name = "Failed processing incoming headers";

  auto method = b->get(grpc_core::HttpMethodMetadata());
  if (method.has_value()) {
    switch (*method) {
      case grpc_core::HttpMethodMetadata::kPost:
        *calld->recv_initial_metadata_flags &=
            ~(GRPC_INITIAL_METADATA_CACHEABLE_REQUEST |
              GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
        break;
      case grpc_core::HttpMethodMetadata::kPut:
        *calld->recv_initial_metadata_flags &=
            ~GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
        *calld->recv_initial_metadata_flags |=
            GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
        break;
      case grpc_core::HttpMethodMetadata::kGet:
        *calld->recv_initial_metadata_flags |=
            GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
        *calld->recv_initial_metadata_flags &=
            ~GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
        break;
      case grpc_core::HttpMethodMetadata::kInvalid:
        hs_add_error(error_name, &error,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad method header"));
        break;
    }
  } else {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, ":method"));
  }

  auto te = b->Take(grpc_core::TeMetadata());
  if (te == grpc_core::TeMetadata::kTrailers) {
    // Do nothing, ok.
  } else if (!te.has_value()) {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, "te"));
  } else {
    hs_add_error(error_name, &error,
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad te header"));
  }

  auto scheme = b->Take(grpc_core::HttpSchemeMetadata());
  if (scheme.has_value()) {
    if (*scheme == grpc_core::HttpSchemeMetadata::kInvalid) {
      hs_add_error(error_name, &error,
                   GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad :scheme header"));
    }
  } else {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, ":scheme"));
  }

  b->Remove(grpc_core::ContentTypeMetadata());

  grpc_core::Slice* path_slice = b->get_pointer(grpc_core::HttpPathMetadata());
  if (path_slice == nullptr) {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, ":path"));
  } else if (*calld->recv_initial_metadata_flags &
             GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) {
    /* We have a cacheable request made with GET verb. The path contains the
     * query parameter which is base64 encoded request payload. */
    static const char kQuerySeparator = '?';
    /* offset of the character '?' */
    auto it =
        std::find(path_slice->begin(), path_slice->end(), kQuerySeparator);
    if (it != path_slice->end()) {
      const auto query_start = it - path_slice->begin() + 1;
      auto query_slice = path_slice->RefSubSlice(
          query_start, path_slice->size() - query_start);

      /* substitute path metadata with just the path (not query) */
      auto path_without_query = path_slice->TakeSubSlice(0, query_start - 1);
      *path_slice = std::move(path_without_query);

      /* decode payload from query and add to the slice buffer to be returned */
      const int k_url_safe = 1;
      grpc_slice_buffer read_slice_buffer;
      grpc_slice_buffer_init(&read_slice_buffer);
      grpc_slice_buffer_add(
          &read_slice_buffer,
          grpc_base64_decode_with_len(
              reinterpret_cast<const char*>(query_slice.begin()),
              query_slice.size(), k_url_safe));
      calld->read_stream.Init(&read_slice_buffer, 0);
      grpc_slice_buffer_destroy_internal(&read_slice_buffer);
      calld->have_read_stream = true;
    } else {
      gpr_log(GPR_ERROR, "GET request without QUERY");
    }
  }

  if (b->get_pointer(grpc_core::HttpAuthorityMetadata()) == nullptr) {
    absl::optional<grpc_core::Slice> host = b->Take(grpc_core::HostMetadata());
    if (host.has_value()) {
      b->Set(grpc_core::HttpAuthorityMetadata(), std::move(*host));
    }
  }

  if (b->get_pointer(grpc_core::HttpAuthorityMetadata()) == nullptr) {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, ":authority"));
  }

  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (!chand->surface_user_agent) {
    b->Remove(grpc_core::UserAgentMetadata());
  }

  return error;
}

static void hs_recv_initial_metadata_ready(void* user_data,
                                           grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->seen_recv_initial_metadata_ready = true;
  if (err == GRPC_ERROR_NONE) {
    err = hs_filter_incoming_metadata(elem, calld->recv_initial_metadata);
    calld->recv_initial_metadata_ready_error = GRPC_ERROR_REF(err);
    if (calld->seen_recv_message_ready) {
      // We've already seen the recv_message callback, but we previously
      // deferred it, so we need to return it here.
      // Replace the recv_message byte stream if needed.
      if (calld->have_read_stream) {
        calld->recv_message->reset(calld->read_stream.get());
        calld->have_read_stream = false;
      }
      // Re-enter call combiner for original_recv_message_ready, since the
      // surface code will release the call combiner for each callback it
      // receives.
      GRPC_CALL_COMBINER_START(
          calld->call_combiner, calld->original_recv_message_ready,
          GRPC_ERROR_REF(err),
          "resuming recv_message_ready from recv_initial_metadata_ready");
    }
  } else {
    (void)GRPC_ERROR_REF(err);
  }
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_ready_error,
                             "resuming hs_recv_trailing_metadata_ready from "
                             "hs_recv_initial_metadata_ready");
  }
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_initial_metadata_ready, err);
}

static void hs_recv_message_ready(void* user_data, grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->seen_recv_message_ready = true;
  if (calld->seen_recv_initial_metadata_ready) {
    // We've already seen the recv_initial_metadata callback, so
    // replace the recv_message byte stream if needed and invoke the
    // original recv_message callback immediately.
    if (calld->have_read_stream) {
      calld->recv_message->reset(calld->read_stream.get());
      calld->have_read_stream = false;
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, calld->original_recv_message_ready,
                            GRPC_ERROR_REF(err));
  } else {
    // We have not yet seen the recv_initial_metadata callback, so we
    // need to wait to see if this is a GET request.
    // Note that we release the call combiner here, so that other
    // callbacks can run.
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner,
        "pausing recv_message_ready until recv_initial_metadata_ready");
  }
}

static void hs_recv_trailing_metadata_ready(void* user_data,
                                            grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (!calld->seen_recv_initial_metadata_ready) {
    calld->recv_trailing_metadata_ready_error = GRPC_ERROR_REF(err);
    calld->seen_recv_trailing_metadata_ready = true;
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring hs_recv_trailing_metadata_ready until "
                            "ater hs_recv_initial_metadata_ready");
    return;
  }
  err = grpc_error_add_child(
      GRPC_ERROR_REF(err),
      GRPC_ERROR_REF(calld->recv_initial_metadata_ready_error));
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_trailing_metadata_ready, err);
}

static grpc_error_handle hs_mutate_op(grpc_call_element* elem,
                                      grpc_transport_stream_op_batch* op) {
  /* grab pointers to our data from the call element */
  call_data* calld = static_cast<call_data*>(elem->call_data);

  if (op->send_initial_metadata) {
    grpc_error_handle error = GRPC_ERROR_NONE;
    static const char* error_name = "Failed sending initial metadata";
    op->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::HttpStatusMetadata(), 200);
    op->payload->send_initial_metadata.send_initial_metadata->Set(
        grpc_core::ContentTypeMetadata(),
        grpc_core::ContentTypeMetadata::kApplicationGrpc);
    hs_add_error(error_name, &error,
                 hs_filter_outgoing_metadata(
                     op->payload->send_initial_metadata.send_initial_metadata));
    if (error != GRPC_ERROR_NONE) return error;
  }

  if (op->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    GPR_ASSERT(op->payload->recv_initial_metadata.recv_flags != nullptr);
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->recv_initial_metadata_flags =
        op->payload->recv_initial_metadata.recv_flags;
    calld->original_recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }

  if (op->recv_message) {
    calld->recv_message = op->payload->recv_message.recv_message;
    calld->original_recv_message_ready =
        op->payload->recv_message.recv_message_ready;
    op->payload->recv_message.recv_message_ready = &calld->recv_message_ready;
  }

  if (op->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }

  if (op->send_trailing_metadata) {
    grpc_error_handle error = hs_filter_outgoing_metadata(
        op->payload->send_trailing_metadata.send_trailing_metadata);
    if (error != GRPC_ERROR_NONE) return error;
  }

  return GRPC_ERROR_NONE;
}

static void hs_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("hs_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_error_handle error = hs_mutate_op(elem, op);
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(op, error,
                                                       calld->call_combiner);
  } else {
    grpc_call_next_op(elem, op);
  }
}

/* Constructor for call_data */
static grpc_error_handle hs_init_call_elem(grpc_call_element* elem,
                                           const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void hs_destroy_call_elem(grpc_call_element* elem,
                                 const grpc_call_final_info* /*final_info*/,
                                 grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

/* Constructor for channel_data */
static grpc_error_handle hs_init_channel_elem(grpc_channel_element* elem,
                                              grpc_channel_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(!args->is_last);
  chand->surface_user_agent = grpc_channel_arg_get_bool(
      grpc_channel_args_find(args->channel_args,
                             const_cast<char*>(GRPC_ARG_SURFACE_USER_AGENT)),
      true);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void hs_destroy_channel_elem(grpc_channel_element* /*elem*/) {}

const grpc_channel_filter grpc_http_server_filter = {
    hs_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    hs_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    hs_destroy_call_elem,
    sizeof(channel_data),
    hs_init_channel_elem,
    hs_destroy_channel_elem,
    grpc_channel_next_get_info,
    "http-server"};
