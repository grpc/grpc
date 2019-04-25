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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <string.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

#define EXPECTED_CONTENT_TYPE "application/grpc"
#define EXPECTED_CONTENT_TYPE_LENGTH sizeof(EXPECTED_CONTENT_TYPE) - 1

static void hs_recv_initial_metadata_ready(void* user_data, grpc_error* err);
static void hs_recv_trailing_metadata_ready(void* user_data, grpc_error* err);
static void hs_recv_message_ready(void* user_data, grpc_error* err);

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

  grpc_call_combiner* call_combiner;

  // Outgoing headers to add to send_initial_metadata.
  grpc_linked_mdelem status;
  grpc_linked_mdelem content_type;

  // If we see the recv_message contents in the GET query string, we
  // store it here.
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> read_stream;
  bool have_read_stream = false;

  // State for intercepting recv_initial_metadata.
  grpc_closure recv_initial_metadata_ready;
  grpc_error* recv_initial_metadata_ready_error = GRPC_ERROR_NONE;
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
  grpc_error* recv_trailing_metadata_ready_error;
  bool seen_recv_trailing_metadata_ready = false;
};

struct channel_data {
  bool surface_user_agent;
};

}  // namespace

static grpc_error* hs_filter_outgoing_metadata(grpc_call_element* elem,
                                               grpc_metadata_batch* b) {
  if (b->idx.named.grpc_message != nullptr) {
    grpc_slice pct_encoded_msg = grpc_percent_encode_slice(
        GRPC_MDVALUE(b->idx.named.grpc_message->md),
        grpc_compatible_percent_encoding_unreserved_bytes);
    if (grpc_slice_is_equivalent(pct_encoded_msg,
                                 GRPC_MDVALUE(b->idx.named.grpc_message->md))) {
      grpc_slice_unref_internal(pct_encoded_msg);
    } else {
      grpc_metadata_batch_set_value(b->idx.named.grpc_message, pct_encoded_msg);
    }
  }
  return GRPC_ERROR_NONE;
}

static void hs_add_error(const char* error_name, grpc_error** cumulative,
                         grpc_error* new_err) {
  if (new_err == GRPC_ERROR_NONE) return;
  if (*cumulative == GRPC_ERROR_NONE) {
    *cumulative = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_name);
  }
  *cumulative = grpc_error_add_child(*cumulative, new_err);
}

static grpc_error* hs_filter_incoming_metadata(grpc_call_element* elem,
                                               grpc_metadata_batch* b) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_error* error = GRPC_ERROR_NONE;
  static const char* error_name = "Failed processing incoming headers";

  if (b->idx.named.method != nullptr) {
    if (grpc_mdelem_eq(b->idx.named.method->md, GRPC_MDELEM_METHOD_POST)) {
      *calld->recv_initial_metadata_flags &=
          ~(GRPC_INITIAL_METADATA_CACHEABLE_REQUEST |
            GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
    } else if (grpc_mdelem_eq(b->idx.named.method->md,
                              GRPC_MDELEM_METHOD_PUT)) {
      *calld->recv_initial_metadata_flags &=
          ~GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
      *calld->recv_initial_metadata_flags |=
          GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
    } else if (grpc_mdelem_eq(b->idx.named.method->md,
                              GRPC_MDELEM_METHOD_GET)) {
      *calld->recv_initial_metadata_flags |=
          GRPC_INITIAL_METADATA_CACHEABLE_REQUEST;
      *calld->recv_initial_metadata_flags &=
          ~GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST;
    } else {
      hs_add_error(error_name, &error,
                   grpc_attach_md_to_error(
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                       b->idx.named.method->md));
    }
    grpc_metadata_batch_remove(b, b->idx.named.method);
  } else {
    hs_add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":method")));
  }

  if (b->idx.named.te != nullptr) {
    if (!grpc_mdelem_eq(b->idx.named.te->md, GRPC_MDELEM_TE_TRAILERS)) {
      hs_add_error(error_name, &error,
                   grpc_attach_md_to_error(
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                       b->idx.named.te->md));
    }
    grpc_metadata_batch_remove(b, b->idx.named.te);
  } else {
    hs_add_error(error_name, &error,
                 grpc_error_set_str(
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                     GRPC_ERROR_STR_KEY, grpc_slice_from_static_string("te")));
  }

  if (b->idx.named.scheme != nullptr) {
    if (!grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_HTTP) &&
        !grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_HTTPS) &&
        !grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_GRPC)) {
      hs_add_error(error_name, &error,
                   grpc_attach_md_to_error(
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                       b->idx.named.scheme->md));
    }
    grpc_metadata_batch_remove(b, b->idx.named.scheme);
  } else {
    hs_add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":scheme")));
  }

  if (b->idx.named.content_type != nullptr) {
    if (!grpc_mdelem_eq(b->idx.named.content_type->md,
                        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC)) {
      if (grpc_slice_buf_start_eq(GRPC_MDVALUE(b->idx.named.content_type->md),
                                  EXPECTED_CONTENT_TYPE,
                                  EXPECTED_CONTENT_TYPE_LENGTH) &&
          (GRPC_SLICE_START_PTR(GRPC_MDVALUE(
               b->idx.named.content_type->md))[EXPECTED_CONTENT_TYPE_LENGTH] ==
               '+' ||
           GRPC_SLICE_START_PTR(GRPC_MDVALUE(
               b->idx.named.content_type->md))[EXPECTED_CONTENT_TYPE_LENGTH] ==
               ';')) {
        /* Although the C implementation doesn't (currently) generate them,
           any custom +-suffix is explicitly valid. */
        /* TODO(klempner): We should consider preallocating common values such
           as +proto or +json, or at least stashing them if we see them. */
        /* TODO(klempner): Should we be surfacing this to application code? */
      } else {
        /* TODO(klempner): We're currently allowing this, but we shouldn't
           see it without a proxy so log for now. */
        char* val = grpc_dump_slice(GRPC_MDVALUE(b->idx.named.content_type->md),
                                    GPR_DUMP_ASCII);
        gpr_log(GPR_INFO, "Unexpected content-type '%s'", val);
        gpr_free(val);
      }
    }
    grpc_metadata_batch_remove(b, b->idx.named.content_type);
  }

  if (b->idx.named.path == nullptr) {
    hs_add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":path")));
  } else if (*calld->recv_initial_metadata_flags &
             GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) {
    /* We have a cacheable request made with GET verb. The path contains the
     * query parameter which is base64 encoded request payload. */
    const char k_query_separator = '?';
    grpc_slice path_slice = GRPC_MDVALUE(b->idx.named.path->md);
    uint8_t* path_ptr = GRPC_SLICE_START_PTR(path_slice);
    size_t path_length = GRPC_SLICE_LENGTH(path_slice);
    /* offset of the character '?' */
    size_t offset = 0;
    for (offset = 0; offset < path_length && *path_ptr != k_query_separator;
         path_ptr++, offset++)
      ;
    if (offset < path_length) {
      grpc_slice query_slice =
          grpc_slice_sub(path_slice, offset + 1, path_length);

      /* substitute path metadata with just the path (not query) */
      grpc_mdelem mdelem_path_without_query = grpc_mdelem_from_slices(
          GRPC_MDSTR_PATH, grpc_slice_sub(path_slice, 0, offset));

      grpc_metadata_batch_substitute(b, b->idx.named.path,
                                     mdelem_path_without_query);

      /* decode payload from query and add to the slice buffer to be returned */
      const int k_url_safe = 1;
      grpc_slice_buffer read_slice_buffer;
      grpc_slice_buffer_init(&read_slice_buffer);
      grpc_slice_buffer_add(
          &read_slice_buffer,
          grpc_base64_decode_with_len(
              reinterpret_cast<const char*> GRPC_SLICE_START_PTR(query_slice),
              GRPC_SLICE_LENGTH(query_slice), k_url_safe));
      calld->read_stream.Init(&read_slice_buffer, 0);
      grpc_slice_buffer_destroy_internal(&read_slice_buffer);
      calld->have_read_stream = true;
      grpc_slice_unref_internal(query_slice);
    } else {
      gpr_log(GPR_ERROR, "GET request without QUERY");
    }
  }

  if (b->idx.named.host != nullptr && b->idx.named.authority == nullptr) {
    grpc_linked_mdelem* el = b->idx.named.host;
    grpc_mdelem md = GRPC_MDELEM_REF(el->md);
    grpc_metadata_batch_remove(b, el);
    hs_add_error(error_name, &error,
                 grpc_metadata_batch_add_head(
                     b, el,
                     grpc_mdelem_from_slices(
                         GRPC_MDSTR_AUTHORITY,
                         grpc_slice_ref_internal(GRPC_MDVALUE(md)))));
    GRPC_MDELEM_UNREF(md);
  }

  if (b->idx.named.authority == nullptr) {
    hs_add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":authority")));
  }

  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (!chand->surface_user_agent && b->idx.named.user_agent != nullptr) {
    grpc_metadata_batch_remove(b, b->idx.named.user_agent);
  }

  return error;
}

static void hs_recv_initial_metadata_ready(void* user_data, grpc_error* err) {
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
    GRPC_ERROR_REF(err);
  }
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_ready_error,
                             "resuming hs_recv_trailing_metadata_ready from "
                             "hs_recv_initial_metadata_ready");
  }
  GRPC_CLOSURE_RUN(calld->original_recv_initial_metadata_ready, err);
}

static void hs_recv_message_ready(void* user_data, grpc_error* err) {
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
    GRPC_CLOSURE_RUN(calld->original_recv_message_ready, GRPC_ERROR_REF(err));
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

static void hs_recv_trailing_metadata_ready(void* user_data, grpc_error* err) {
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
  GRPC_CLOSURE_RUN(calld->original_recv_trailing_metadata_ready, err);
}

static grpc_error* hs_mutate_op(grpc_call_element* elem,
                                grpc_transport_stream_op_batch* op) {
  /* grab pointers to our data from the call element */
  call_data* calld = static_cast<call_data*>(elem->call_data);

  if (op->send_initial_metadata) {
    grpc_error* error = GRPC_ERROR_NONE;
    static const char* error_name = "Failed sending initial metadata";
    hs_add_error(error_name, &error,
                 grpc_metadata_batch_add_head(
                     op->payload->send_initial_metadata.send_initial_metadata,
                     &calld->status, GRPC_MDELEM_STATUS_200));
    hs_add_error(error_name, &error,
                 grpc_metadata_batch_add_tail(
                     op->payload->send_initial_metadata.send_initial_metadata,
                     &calld->content_type,
                     GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC));
    hs_add_error(
        error_name, &error,
        hs_filter_outgoing_metadata(
            elem, op->payload->send_initial_metadata.send_initial_metadata));
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
    grpc_error* error = hs_filter_outgoing_metadata(
        elem, op->payload->send_trailing_metadata.send_trailing_metadata);
    if (error != GRPC_ERROR_NONE) return error;
  }

  return GRPC_ERROR_NONE;
}

static void hs_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("hs_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_error* error = hs_mutate_op(elem, op);
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(op, error,
                                                       calld->call_combiner);
  } else {
    grpc_call_next_op(elem, op);
  }
}

/* Constructor for call_data */
static grpc_error* hs_init_call_elem(grpc_call_element* elem,
                                     const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void hs_destroy_call_elem(grpc_call_element* elem,
                                 const grpc_call_final_info* final_info,
                                 grpc_closure* ignored) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

/* Constructor for channel_data */
static grpc_error* hs_init_channel_elem(grpc_channel_element* elem,
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
static void hs_destroy_channel_elem(grpc_channel_element* elem) {}

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
