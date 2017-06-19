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

#include "src/core/ext/filters/http/server/http_server_filter.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <string.h>
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

#define EXPECTED_CONTENT_TYPE "application/grpc"
#define EXPECTED_CONTENT_TYPE_LENGTH sizeof(EXPECTED_CONTENT_TYPE) - 1

typedef struct call_data {
  grpc_linked_mdelem status;
  grpc_linked_mdelem content_type;

  /* did this request come with path query containing request payload */
  bool seen_path_with_query;
  /* flag to ensure payload_bin is delivered only once */
  bool payload_bin_delivered;

  grpc_metadata_batch *recv_initial_metadata;
  uint32_t *recv_initial_metadata_flags;
  /** Closure to call when finished with the hs_on_recv hook */
  grpc_closure *on_done_recv;
  /** Closure to call when we retrieve read message from the path URI
   */
  grpc_closure *recv_message_ready;
  grpc_closure *on_complete;
  grpc_byte_stream **pp_recv_message;
  grpc_slice_buffer read_slice_buffer;
  grpc_slice_buffer_stream read_stream;

  /** Receive closures are chained: we inject this closure as the on_done_recv
      up-call on transport_op, and remember to call our on_done_recv member
      after handling it. */
  grpc_closure hs_on_recv;
  grpc_closure hs_on_complete;
  grpc_closure hs_recv_message_ready;
} call_data;

typedef struct channel_data { uint8_t unused; } channel_data;

static grpc_error *server_filter_outgoing_metadata(grpc_exec_ctx *exec_ctx,
                                                   grpc_call_element *elem,
                                                   grpc_metadata_batch *b) {
  if (b->idx.named.grpc_message != NULL) {
    grpc_slice pct_encoded_msg = grpc_percent_encode_slice(
        GRPC_MDVALUE(b->idx.named.grpc_message->md),
        grpc_compatible_percent_encoding_unreserved_bytes);
    if (grpc_slice_is_equivalent(pct_encoded_msg,
                                 GRPC_MDVALUE(b->idx.named.grpc_message->md))) {
      grpc_slice_unref_internal(exec_ctx, pct_encoded_msg);
    } else {
      grpc_metadata_batch_set_value(exec_ctx, b->idx.named.grpc_message,
                                    pct_encoded_msg);
    }
  }
  return GRPC_ERROR_NONE;
}

static void add_error(const char *error_name, grpc_error **cumulative,
                      grpc_error *new) {
  if (new == GRPC_ERROR_NONE) return;
  if (*cumulative == GRPC_ERROR_NONE) {
    *cumulative = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_name);
  }
  *cumulative = grpc_error_add_child(*cumulative, new);
}

static grpc_error *server_filter_incoming_metadata(grpc_exec_ctx *exec_ctx,
                                                   grpc_call_element *elem,
                                                   grpc_metadata_batch *b) {
  call_data *calld = elem->call_data;
  grpc_error *error = GRPC_ERROR_NONE;
  static const char *error_name = "Failed processing incoming headers";

  if (b->idx.named.method != NULL) {
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
      add_error(error_name, &error,
                grpc_attach_md_to_error(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                    b->idx.named.method->md));
    }
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.method);
  } else {
    add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":method")));
  }

  if (b->idx.named.te != NULL) {
    if (!grpc_mdelem_eq(b->idx.named.te->md, GRPC_MDELEM_TE_TRAILERS)) {
      add_error(error_name, &error,
                grpc_attach_md_to_error(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                    b->idx.named.te->md));
    }
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.te);
  } else {
    add_error(error_name, &error,
              grpc_error_set_str(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                  GRPC_ERROR_STR_KEY, grpc_slice_from_static_string("te")));
  }

  if (b->idx.named.scheme != NULL) {
    if (!grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_HTTP) &&
        !grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_HTTPS) &&
        !grpc_mdelem_eq(b->idx.named.scheme->md, GRPC_MDELEM_SCHEME_GRPC)) {
      add_error(error_name, &error,
                grpc_attach_md_to_error(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad header"),
                    b->idx.named.scheme->md));
    }
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.scheme);
  } else {
    add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":scheme")));
  }

  if (b->idx.named.content_type != NULL) {
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
        char *val = grpc_dump_slice(GRPC_MDVALUE(b->idx.named.content_type->md),
                                    GPR_DUMP_ASCII);
        gpr_log(GPR_INFO, "Unexpected content-type '%s'", val);
        gpr_free(val);
      }
    }
    grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.content_type);
  }

  if (b->idx.named.path == NULL) {
    add_error(error_name, &error,
              grpc_error_set_str(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
                  GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":path")));
  } else if (*calld->recv_initial_metadata_flags &
             GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) {
    /* We have a cacheable request made with GET verb. The path contains the
     * query parameter which is base64 encoded request payload. */
    const char k_query_separator = '?';
    grpc_slice path_slice = GRPC_MDVALUE(b->idx.named.path->md);
    uint8_t *path_ptr = (uint8_t *)GRPC_SLICE_START_PTR(path_slice);
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
          exec_ctx, GRPC_MDSTR_PATH, grpc_slice_sub(path_slice, 0, offset));

      grpc_metadata_batch_substitute(exec_ctx, b, b->idx.named.path,
                                     mdelem_path_without_query);

      /* decode payload from query and add to the slice buffer to be returned */
      const int k_url_safe = 1;
      grpc_slice_buffer_add(
          &calld->read_slice_buffer,
          grpc_base64_decode_with_len(
              exec_ctx, (const char *)GRPC_SLICE_START_PTR(query_slice),
              GRPC_SLICE_LENGTH(query_slice), k_url_safe));
      grpc_slice_buffer_stream_init(&calld->read_stream,
                                    &calld->read_slice_buffer, 0);
      calld->seen_path_with_query = true;
      grpc_slice_unref_internal(exec_ctx, query_slice);
    } else {
      gpr_log(GPR_ERROR, "GET request without QUERY");
    }
  }

  if (b->idx.named.host != NULL && b->idx.named.authority == NULL) {
    grpc_linked_mdelem *el = b->idx.named.host;
    grpc_mdelem md = GRPC_MDELEM_REF(el->md);
    grpc_metadata_batch_remove(exec_ctx, b, el);
    add_error(
        error_name, &error,
        grpc_metadata_batch_add_head(
            exec_ctx, b, el, grpc_mdelem_from_slices(
                                 exec_ctx, GRPC_MDSTR_AUTHORITY,
                                 grpc_slice_ref_internal(GRPC_MDVALUE(md)))));
    GRPC_MDELEM_UNREF(exec_ctx, md);
  }

  if (b->idx.named.authority == NULL) {
    add_error(
        error_name, &error,
        grpc_error_set_str(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing header"),
            GRPC_ERROR_STR_KEY, grpc_slice_from_static_string(":authority")));
  }

  return error;
}

static void hs_on_recv(grpc_exec_ctx *exec_ctx, void *user_data,
                       grpc_error *err) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (err == GRPC_ERROR_NONE) {
    err = server_filter_incoming_metadata(exec_ctx, elem,
                                          calld->recv_initial_metadata);
  } else {
    GRPC_ERROR_REF(err);
  }
  GRPC_CLOSURE_RUN(exec_ctx, calld->on_done_recv, err);
}

static void hs_on_complete(grpc_exec_ctx *exec_ctx, void *user_data,
                           grpc_error *err) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  /* Call recv_message_ready if we got the payload via the path field */
  if (calld->seen_path_with_query && calld->recv_message_ready != NULL) {
    *calld->pp_recv_message = calld->payload_bin_delivered
                                  ? NULL
                                  : (grpc_byte_stream *)&calld->read_stream;
    GRPC_CLOSURE_RUN(exec_ctx, calld->recv_message_ready, GRPC_ERROR_REF(err));
    calld->recv_message_ready = NULL;
    calld->payload_bin_delivered = true;
  }
  GRPC_CLOSURE_RUN(exec_ctx, calld->on_complete, GRPC_ERROR_REF(err));
}

static void hs_recv_message_ready(grpc_exec_ctx *exec_ctx, void *user_data,
                                  grpc_error *err) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (calld->seen_path_with_query) {
    /* do nothing. This is probably a GET request, and payload will be returned
    in hs_on_complete callback. */
  } else {
    GRPC_CLOSURE_RUN(exec_ctx, calld->recv_message_ready, GRPC_ERROR_REF(err));
  }
}

static void hs_mutate_op(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                         grpc_transport_stream_op_batch *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;

  if (op->send_initial_metadata) {
    grpc_error *error = GRPC_ERROR_NONE;
    static const char *error_name = "Failed sending initial metadata";
    add_error(
        error_name, &error,
        grpc_metadata_batch_add_head(
            exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
            &calld->status, GRPC_MDELEM_STATUS_200));
    add_error(
        error_name, &error,
        grpc_metadata_batch_add_tail(
            exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
            &calld->content_type,
            GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC));
    add_error(error_name, &error,
              server_filter_outgoing_metadata(
                  exec_ctx, elem,
                  op->payload->send_initial_metadata.send_initial_metadata));
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
      return;
    }
  }

  if (op->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    GPR_ASSERT(op->payload->recv_initial_metadata.recv_flags != NULL);
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->recv_initial_metadata_flags =
        op->payload->recv_initial_metadata.recv_flags;
    calld->on_done_recv =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->hs_on_recv;
  }

  if (op->recv_message) {
    calld->recv_message_ready = op->payload->recv_message.recv_message_ready;
    calld->pp_recv_message = op->payload->recv_message.recv_message;
    if (op->payload->recv_message.recv_message_ready) {
      op->payload->recv_message.recv_message_ready =
          &calld->hs_recv_message_ready;
    }
    if (op->on_complete) {
      calld->on_complete = op->on_complete;
      op->on_complete = &calld->hs_on_complete;
    }
  }

  if (op->send_trailing_metadata) {
    grpc_error *error = server_filter_outgoing_metadata(
        exec_ctx, elem,
        op->payload->send_trailing_metadata.send_trailing_metadata);
    if (error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
      return;
    }
  }
}

static void hs_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  grpc_transport_stream_op_batch *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  GPR_TIMER_BEGIN("hs_start_transport_op", 0);
  hs_mutate_op(exec_ctx, elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
  GPR_TIMER_END("hs_start_transport_op", 0);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  /* initialize members */
  GRPC_CLOSURE_INIT(&calld->hs_on_recv, hs_on_recv, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->hs_on_complete, hs_on_complete, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&calld->hs_recv_message_ready, hs_recv_message_ready, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&calld->read_slice_buffer);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  call_data *calld = elem->call_data;
  grpc_slice_buffer_destroy_internal(exec_ctx, &calld->read_slice_buffer);
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {}

const grpc_channel_filter grpc_http_server_filter = {
    hs_start_transport_op,
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
    "http-server"};
