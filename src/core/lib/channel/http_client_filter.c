/*
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/channel/http_client_filter.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/transport_impl.h"

#define EXPECTED_CONTENT_TYPE "application/grpc"
#define EXPECTED_CONTENT_TYPE_LENGTH sizeof(EXPECTED_CONTENT_TYPE) - 1

/* default maximum size of payload eligable for GET request */
static const size_t kMaxPayloadSizeForGet = 2048;

typedef struct call_data {
  grpc_linked_mdelem method;
  grpc_linked_mdelem scheme;
  grpc_linked_mdelem authority;
  grpc_linked_mdelem te_trailers;
  grpc_linked_mdelem content_type;
  grpc_linked_mdelem user_agent;

  grpc_metadata_batch *recv_initial_metadata;
  grpc_metadata_batch *recv_trailing_metadata;
  uint8_t *payload_bytes;

  /* Vars to read data off of send_message */
  grpc_transport_stream_op_batch *send_op;
  uint32_t send_length;
  uint32_t send_flags;
  grpc_slice incoming_slice;
  grpc_slice_buffer_stream replacement_stream;
  grpc_slice_buffer slices;
  /* flag that indicates that all slices of send_messages aren't availble */
  bool send_message_blocked;

  /** Closure to call when finished with the hc_on_recv hook */
  grpc_closure *on_done_recv_initial_metadata;
  grpc_closure *on_done_recv_trailing_metadata;
  grpc_closure *on_complete;
  grpc_closure *post_send;

  /** Receive closures are chained: we inject this closure as the on_done_recv
      up-call on transport_op, and remember to call our on_done_recv member
      after handling it. */
  grpc_closure hc_on_recv_initial_metadata;
  grpc_closure hc_on_recv_trailing_metadata;
  grpc_closure hc_on_complete;
  grpc_closure got_slice;
  grpc_closure send_done;
} call_data;

typedef struct channel_data {
  grpc_mdelem static_scheme;
  grpc_mdelem user_agent;
  size_t max_payload_size_for_get;
} channel_data;

static grpc_error *client_filter_incoming_metadata(grpc_exec_ctx *exec_ctx,
                                                   grpc_call_element *elem,
                                                   grpc_metadata_batch *b) {
  if (b->idx.named.status != NULL) {
    if (grpc_mdelem_eq(b->idx.named.status->md, GRPC_MDELEM_STATUS_200)) {
      grpc_metadata_batch_remove(exec_ctx, b, b->idx.named.status);
    } else {
      char *val = grpc_dump_slice(GRPC_MDVALUE(b->idx.named.status->md),
                                  GPR_DUMP_ASCII);
      char *msg;
      gpr_asprintf(&msg, "Received http2 header with status: %s", val);
      grpc_error *e = grpc_error_set_str(
          grpc_error_set_int(
              grpc_error_set_str(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "Received http2 :status header with non-200 OK status"),
                  GRPC_ERROR_STR_VALUE, grpc_slice_from_copied_string(val)),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED),
          GRPC_ERROR_STR_GRPC_MESSAGE, grpc_slice_from_copied_string(msg));
      gpr_free(val);
      gpr_free(msg);
      return e;
    }
  }

  if (b->idx.named.grpc_message != NULL) {
    grpc_slice pct_decoded_msg = grpc_permissive_percent_decode_slice(
        GRPC_MDVALUE(b->idx.named.grpc_message->md));
    if (grpc_slice_is_equivalent(pct_decoded_msg,
                                 GRPC_MDVALUE(b->idx.named.grpc_message->md))) {
      grpc_slice_unref_internal(exec_ctx, pct_decoded_msg);
    } else {
      grpc_metadata_batch_set_value(exec_ctx, b->idx.named.grpc_message,
                                    pct_decoded_msg);
    }
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

  return GRPC_ERROR_NONE;
}

static void hc_on_recv_initial_metadata(grpc_exec_ctx *exec_ctx,
                                        void *user_data, grpc_error *error) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (error == GRPC_ERROR_NONE) {
    error = client_filter_incoming_metadata(exec_ctx, elem,
                                            calld->recv_initial_metadata);
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure_run(exec_ctx, calld->on_done_recv_initial_metadata, error);
}

static void hc_on_recv_trailing_metadata(grpc_exec_ctx *exec_ctx,
                                         void *user_data, grpc_error *error) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (error == GRPC_ERROR_NONE) {
    error = client_filter_incoming_metadata(exec_ctx, elem,
                                            calld->recv_trailing_metadata);
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure_run(exec_ctx, calld->on_done_recv_trailing_metadata, error);
}

static void hc_on_complete(grpc_exec_ctx *exec_ctx, void *user_data,
                           grpc_error *error) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (calld->payload_bytes) {
    gpr_free(calld->payload_bytes);
    calld->payload_bytes = NULL;
  }
  calld->on_complete->cb(exec_ctx, calld->on_complete->cb_arg, error);
}

static void send_done(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
  call_data *calld = elem->call_data;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &calld->slices);
  calld->post_send->cb(exec_ctx, calld->post_send->cb_arg, error);
}

static void remove_if_present(grpc_exec_ctx *exec_ctx,
                              grpc_metadata_batch *batch,
                              grpc_metadata_batch_callouts_index idx) {
  if (batch->idx.array[idx] != NULL) {
    grpc_metadata_batch_remove(exec_ctx, batch, batch->idx.array[idx]);
  }
}

static void continue_send_message(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  uint8_t *wrptr = calld->payload_bytes;
  while (grpc_byte_stream_next(
      exec_ctx, calld->send_op->payload->send_message.send_message,
      &calld->incoming_slice, ~(size_t)0, &calld->got_slice)) {
    memcpy(wrptr, GRPC_SLICE_START_PTR(calld->incoming_slice),
           GRPC_SLICE_LENGTH(calld->incoming_slice));
    wrptr += GRPC_SLICE_LENGTH(calld->incoming_slice);
    grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
    if (calld->send_length == calld->slices.length) {
      calld->send_message_blocked = false;
      break;
    }
  }
}

static void got_slice(grpc_exec_ctx *exec_ctx, void *elemp, grpc_error *error) {
  grpc_call_element *elem = elemp;
  call_data *calld = elem->call_data;
  calld->send_message_blocked = false;
  grpc_slice_buffer_add(&calld->slices, calld->incoming_slice);
  if (calld->send_length == calld->slices.length) {
    /* Pass down the original send_message op that was blocked.*/
    grpc_slice_buffer_stream_init(&calld->replacement_stream, &calld->slices,
                                  calld->send_flags);
    calld->send_op->payload->send_message.send_message =
        &calld->replacement_stream.base;
    calld->post_send = calld->send_op->on_complete;
    calld->send_op->on_complete = &calld->send_done;
    grpc_call_next_op(exec_ctx, elem, calld->send_op);
  } else {
    continue_send_message(exec_ctx, elem);
  }
}

static grpc_error *hc_mutate_op(grpc_exec_ctx *exec_ctx,
                                grpc_call_element *elem,
                                grpc_transport_stream_op_batch *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  grpc_error *error;

  if (op->send_initial_metadata) {
    /* Decide which HTTP VERB to use. We use GET if the request is marked
    cacheable, and the operation contains both initial metadata and send
    message, and the payload is below the size threshold, and all the data
    for this request is immediately available. */
    grpc_mdelem method = GRPC_MDELEM_METHOD_POST;
    if (op->send_message &&
        (op->payload->send_initial_metadata.send_initial_metadata_flags &
         GRPC_INITIAL_METADATA_CACHEABLE_REQUEST) &&
        op->payload->send_message.send_message->length <
            channeld->max_payload_size_for_get) {
      method = GRPC_MDELEM_METHOD_GET;
      /* The following write to calld->send_message_blocked isn't racy with
      reads in hc_start_transport_op (which deals with SEND_MESSAGE ops) because
      being here means ops->send_message is not NULL, which is primarily
      guarding the read there. */
      calld->send_message_blocked = true;
    } else if (op->payload->send_initial_metadata.send_initial_metadata_flags &
               GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST) {
      method = GRPC_MDELEM_METHOD_PUT;
    }

    /* Attempt to read the data from send_message and create a header field. */
    if (grpc_mdelem_eq(method, GRPC_MDELEM_METHOD_GET)) {
      /* allocate memory to hold the entire payload */
      calld->payload_bytes =
          gpr_malloc(op->payload->send_message.send_message->length);

      /* read slices of send_message and copy into payload_bytes */
      calld->send_op = op;
      calld->send_length = op->payload->send_message.send_message->length;
      calld->send_flags = op->payload->send_message.send_message->flags;
      continue_send_message(exec_ctx, elem);

      if (calld->send_message_blocked == false) {
        /* when all the send_message data is available, then modify the path
         * MDELEM by appending base64 encoded query to the path */
        const int k_url_safe = 1;
        const int k_multi_line = 0;
        const unsigned char k_query_separator = '?';

        grpc_slice path_slice =
            GRPC_MDVALUE(op->payload->send_initial_metadata
                             .send_initial_metadata->idx.named.path->md);
        /* sum up individual component's lengths and allocate enough memory to
         * hold combined path+query */
        size_t estimated_len = GRPC_SLICE_LENGTH(path_slice);
        estimated_len++; /* for the '?' */
        estimated_len += grpc_base64_estimate_encoded_size(
            op->payload->send_message.send_message->length, k_url_safe,
            k_multi_line);
        estimated_len += 1; /* for the trailing 0 */
        grpc_slice path_with_query_slice = grpc_slice_malloc(estimated_len);

        /* memcopy individual pieces into this slice */
        uint8_t *write_ptr =
            (uint8_t *)GRPC_SLICE_START_PTR(path_with_query_slice);
        uint8_t *original_path = (uint8_t *)GRPC_SLICE_START_PTR(path_slice);
        memcpy(write_ptr, original_path, GRPC_SLICE_LENGTH(path_slice));
        write_ptr += GRPC_SLICE_LENGTH(path_slice);

        *write_ptr = k_query_separator;
        write_ptr++; /* for the '?' */

        grpc_base64_encode_core((char *)write_ptr, calld->payload_bytes,
                                op->payload->send_message.send_message->length,
                                k_url_safe, k_multi_line);

        /* remove trailing unused memory and add trailing 0 to terminate string
         */
        char *t = (char *)GRPC_SLICE_START_PTR(path_with_query_slice);
        /* safe to use strlen since base64_encode will always add '\0' */
        size_t path_length = strlen(t) + 1;
        *(t + path_length) = '\0';
        path_with_query_slice =
            grpc_slice_sub(path_with_query_slice, 0, path_length);

        /* substitute previous path with the new path+query */
        grpc_mdelem mdelem_path_and_query = grpc_mdelem_from_slices(
            exec_ctx, GRPC_MDSTR_PATH, path_with_query_slice);
        grpc_metadata_batch *b =
            op->payload->send_initial_metadata.send_initial_metadata;
        error = grpc_metadata_batch_substitute(exec_ctx, b, b->idx.named.path,
                                               mdelem_path_and_query);
        if (error != GRPC_ERROR_NONE) return error;

        calld->on_complete = op->on_complete;
        op->on_complete = &calld->hc_on_complete;
        op->send_message = false;
        grpc_slice_unref_internal(exec_ctx, path_with_query_slice);
      } else {
        /* Not all data is available. Fall back to POST. */
        gpr_log(GPR_DEBUG,
                "Request is marked Cacheable but not all data is available.\
                            Falling back to POST");
        method = GRPC_MDELEM_METHOD_POST;
      }
    }

    remove_if_present(exec_ctx,
                      op->payload->send_initial_metadata.send_initial_metadata,
                      GRPC_BATCH_METHOD);
    remove_if_present(exec_ctx,
                      op->payload->send_initial_metadata.send_initial_metadata,
                      GRPC_BATCH_SCHEME);
    remove_if_present(exec_ctx,
                      op->payload->send_initial_metadata.send_initial_metadata,
                      GRPC_BATCH_TE);
    remove_if_present(exec_ctx,
                      op->payload->send_initial_metadata.send_initial_metadata,
                      GRPC_BATCH_CONTENT_TYPE);
    remove_if_present(exec_ctx,
                      op->payload->send_initial_metadata.send_initial_metadata,
                      GRPC_BATCH_USER_AGENT);

    /* Send : prefixed headers, which have to be before any application
       layer headers. */
    error = grpc_metadata_batch_add_head(
        exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
        &calld->method, method);
    if (error != GRPC_ERROR_NONE) return error;
    error = grpc_metadata_batch_add_head(
        exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
        &calld->scheme, channeld->static_scheme);
    if (error != GRPC_ERROR_NONE) return error;
    error = grpc_metadata_batch_add_tail(
        exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
        &calld->te_trailers, GRPC_MDELEM_TE_TRAILERS);
    if (error != GRPC_ERROR_NONE) return error;
    error = grpc_metadata_batch_add_tail(
        exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
        &calld->content_type, GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC);
    if (error != GRPC_ERROR_NONE) return error;
    error = grpc_metadata_batch_add_tail(
        exec_ctx, op->payload->send_initial_metadata.send_initial_metadata,
        &calld->user_agent, GRPC_MDELEM_REF(channeld->user_agent));
    if (error != GRPC_ERROR_NONE) return error;
  }

  if (op->recv_initial_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->on_done_recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->hc_on_recv_initial_metadata;
  }

  if (op->recv_trailing_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    calld->on_done_recv_trailing_metadata = op->on_complete;
    op->on_complete = &calld->hc_on_recv_trailing_metadata;
  }

  return GRPC_ERROR_NONE;
}

static void hc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  grpc_transport_stream_op_batch *op) {
  GPR_TIMER_BEGIN("hc_start_transport_op", 0);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  grpc_error *error = hc_mutate_op(exec_ctx, elem, op);
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
  } else {
    call_data *calld = elem->call_data;
    if (op->send_message && calld->send_message_blocked) {
      /* Don't forward the op. send_message contains slices that aren't ready
         yet. The call will be forwarded by the op_complete of slice read call.
      */
    } else {
      grpc_call_next_op(exec_ctx, elem, op);
    }
  }
  GPR_TIMER_END("hc_start_transport_op", 0);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  calld->on_done_recv_initial_metadata = NULL;
  calld->on_done_recv_trailing_metadata = NULL;
  calld->on_complete = NULL;
  calld->payload_bytes = NULL;
  calld->send_message_blocked = false;
  grpc_slice_buffer_init(&calld->slices);
  grpc_closure_init(&calld->hc_on_recv_initial_metadata,
                    hc_on_recv_initial_metadata, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&calld->hc_on_recv_trailing_metadata,
                    hc_on_recv_trailing_metadata, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&calld->hc_on_complete, hc_on_complete, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&calld->got_slice, got_slice, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&calld->send_done, send_done, elem,
                    grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  call_data *calld = elem->call_data;
  grpc_slice_buffer_destroy_internal(exec_ctx, &calld->slices);
}

static grpc_mdelem scheme_from_args(const grpc_channel_args *args) {
  unsigned i;
  size_t j;
  grpc_mdelem valid_schemes[] = {GRPC_MDELEM_SCHEME_HTTP,
                                 GRPC_MDELEM_SCHEME_HTTPS};
  if (args != NULL) {
    for (i = 0; i < args->num_args; ++i) {
      if (args->args[i].type == GRPC_ARG_STRING &&
          strcmp(args->args[i].key, GRPC_ARG_HTTP2_SCHEME) == 0) {
        for (j = 0; j < GPR_ARRAY_SIZE(valid_schemes); j++) {
          if (0 == grpc_slice_str_cmp(GRPC_MDVALUE(valid_schemes[j]),
                                      args->args[i].value.string)) {
            return valid_schemes[j];
          }
        }
      }
    }
  }
  return GRPC_MDELEM_SCHEME_HTTP;
}

static size_t max_payload_size_from_args(const grpc_channel_args *args) {
  if (args != NULL) {
    for (size_t i = 0; i < args->num_args; ++i) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_MAX_PAYLOAD_SIZE_FOR_GET)) {
        if (args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_MAX_PAYLOAD_SIZE_FOR_GET);
        } else {
          return (size_t)args->args[i].value.integer;
        }
      }
    }
  }
  return kMaxPayloadSizeForGet;
}

static grpc_slice user_agent_from_args(const grpc_channel_args *args,
                                       const char *transport_name) {
  gpr_strvec v;
  size_t i;
  int is_first = 1;
  char *tmp;
  grpc_slice result;

  gpr_strvec_init(&v);

  for (i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_PRIMARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_PRIMARY_USER_AGENT_STRING);
      } else {
        if (!is_first) gpr_strvec_add(&v, gpr_strdup(" "));
        is_first = 0;
        gpr_strvec_add(&v, gpr_strdup(args->args[i].value.string));
      }
    }
  }

  gpr_asprintf(&tmp, "%sgrpc-c/%s (%s; %s; %s)", is_first ? "" : " ",
               grpc_version_string(), GPR_PLATFORM_STRING, transport_name,
               grpc_g_stands_for());
  is_first = 0;
  gpr_strvec_add(&v, tmp);

  for (i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_SECONDARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_SECONDARY_USER_AGENT_STRING);
      } else {
        if (!is_first) gpr_strvec_add(&v, gpr_strdup(" "));
        is_first = 0;
        gpr_strvec_add(&v, gpr_strdup(args->args[i].value.string));
      }
    }
  }

  tmp = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  result = grpc_slice_intern(grpc_slice_from_static_string(tmp));
  gpr_free(tmp);

  return result;
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(!args->is_last);
  GPR_ASSERT(args->optional_transport != NULL);
  chand->static_scheme = scheme_from_args(args->channel_args);
  chand->max_payload_size_for_get =
      max_payload_size_from_args(args->channel_args);
  chand->user_agent = grpc_mdelem_from_slices(
      exec_ctx, GRPC_MDSTR_USER_AGENT,
      user_agent_from_args(args->channel_args,
                           args->optional_transport->vtable->name));
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  GRPC_MDELEM_UNREF(exec_ctx, chand->user_agent);
}

const grpc_channel_filter grpc_http_client_filter = {
    hc_start_transport_op,
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
    "http-client"};
