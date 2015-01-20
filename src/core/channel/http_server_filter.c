/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/channel/http_server_filter.h"

#include <string.h>
#include <grpc/support/log.h>

typedef struct call_data {
  gpr_uint8 sent_status;
  gpr_uint8 seen_scheme;
  gpr_uint8 seen_method;
  gpr_uint8 seen_te_trailers;
  grpc_mdelem *path;
} call_data;

typedef struct channel_data {
  grpc_mdelem *te_trailers;
  grpc_mdelem *method;
  grpc_mdelem *http_scheme;
  grpc_mdelem *https_scheme;
  /* TODO(klempner): Remove this once we stop using it */
  grpc_mdelem *grpc_scheme;
  grpc_mdelem *content_type;
  grpc_mdelem *status;
  grpc_mdstr *path_key;
} channel_data;

/* used to silence 'variable not used' warnings */
static void ignore_unused(void *ignored) {}

/* Called either:
     - in response to an API call (or similar) from above, to send something
     - a network event (or similar) from below, to receive something
   op contains type and call direction information, in addition to the data
   that is being sent or received. */
static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  switch (op->type) {
    case GRPC_RECV_METADATA:
      /* Check if it is one of the headers we care about. */
      if (op->data.metadata == channeld->te_trailers ||
          op->data.metadata == channeld->method ||
          op->data.metadata == channeld->http_scheme ||
          op->data.metadata == channeld->https_scheme ||
          op->data.metadata == channeld->grpc_scheme ||
          op->data.metadata == channeld->content_type) {
        /* swallow it */
        if (op->data.metadata == channeld->method) {
          calld->seen_method = 1;
        } else if (op->data.metadata->key == channeld->http_scheme->key) {
          calld->seen_scheme = 1;
        } else if (op->data.metadata == channeld->te_trailers) {
          calld->seen_te_trailers = 1;
        }
        /* TODO(klempner): Track that we've seen all the headers we should
           require */
        grpc_mdelem_unref(op->data.metadata);
        op->done_cb(op->user_data, GRPC_OP_OK);
      } else if (op->data.metadata->key == channeld->content_type->key) {
        if (strncmp(grpc_mdstr_as_c_string(op->data.metadata->value),
                    "application/grpc+", 17) == 0) {
          /* Although the C implementation doesn't (currently) generate them,
             any
             custom +-suffix is explicitly valid. */
          /* TODO(klempner): We should consider preallocating common values such
             as +proto or +json, or at least stashing them if we see them. */
          /* TODO(klempner): Should we be surfacing this to application code? */
        } else {
          /* TODO(klempner): We're currently allowing this, but we shouldn't
             see it without a proxy so log for now. */
          gpr_log(GPR_INFO, "Unexpected content-type %s",
                  channeld->content_type->key);
        }
        grpc_mdelem_unref(op->data.metadata);
        op->done_cb(op->user_data, GRPC_OP_OK);
      } else if (op->data.metadata->key == channeld->te_trailers->key ||
                 op->data.metadata->key == channeld->method->key ||
                 op->data.metadata->key == channeld->http_scheme->key ||
                 op->data.metadata->key == channeld->content_type->key) {
        gpr_log(GPR_ERROR, "Invalid %s: header: '%s'",
                grpc_mdstr_as_c_string(op->data.metadata->key),
                grpc_mdstr_as_c_string(op->data.metadata->value));
        /* swallow it and error everything out. */
        /* TODO(klempner): We ought to generate more descriptive error messages
           on the wire here. */
        grpc_mdelem_unref(op->data.metadata);
        op->done_cb(op->user_data, GRPC_OP_OK);
        grpc_call_element_send_cancel(elem);
      } else if (op->data.metadata->key == channeld->path_key) {
        if (calld->path != NULL) {
          gpr_log(GPR_ERROR, "Received :path twice");
          grpc_mdelem_unref(calld->path);
        }
        calld->path = op->data.metadata;
        op->done_cb(op->user_data, GRPC_OP_OK);
      } else {
        /* pass the event up */
        grpc_call_next_op(elem, op);
      }
      break;
    case GRPC_RECV_END_OF_INITIAL_METADATA:
      /* Have we seen the required http2 transport headers?
         (:method, :scheme, content-type, with :path and :authority covered
         at the channel level right now) */
      if (calld->seen_method && calld->seen_scheme && calld->seen_te_trailers &&
          calld->path) {
        grpc_call_element_recv_metadata(elem, calld->path);
        calld->path = NULL;
        grpc_call_next_op(elem, op);
      } else {
        if (!calld->seen_method) {
          gpr_log(GPR_ERROR, "Missing :method header");
        } else if (!calld->seen_scheme) {
          gpr_log(GPR_ERROR, "Missing :scheme header");
        } else if (!calld->seen_te_trailers) {
          gpr_log(GPR_ERROR, "Missing te trailers header");
        }
        /* Error this call out */
        op->done_cb(op->user_data, GRPC_OP_OK);
        grpc_call_element_send_cancel(elem);
      }
      break;
    case GRPC_SEND_START:
    case GRPC_SEND_METADATA:
      /* If we haven't sent status 200 yet, we need to so so because it needs to
         come before any non : prefixed metadata. */
      if (!calld->sent_status) {
        calld->sent_status = 1;
        /* status is reffed by grpc_call_element_send_metadata */
        grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->status));
      }
      grpc_call_next_op(elem, op);
      break;
    default:
      /* pass control up or down the stack depending on op->dir */
      grpc_call_next_op(elem, op);
      break;
  }
}

/* Called on special channel events, such as disconnection or new incoming
   calls on the server */
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  ignore_unused(channeld);

  switch (op->type) {
    default:
      /* pass control up or down the stack depending on op->dir */
      grpc_channel_next_op(elem, op);
      break;
  }
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  ignore_unused(channeld);

  /* initialize members */
  calld->path = NULL;
  calld->sent_status = 0;
  calld->seen_scheme = 0;
  calld->seen_method = 0;
  calld->seen_te_trailers = 0;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  ignore_unused(channeld);

  if (calld->path) {
    grpc_mdelem_unref(calld->path);
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  /* The first and the last filters tend to be implemented differently to
     handle the case that there's no 'next' filter to call on the up or down
     path */
  GPR_ASSERT(!is_first);
  GPR_ASSERT(!is_last);

  /* initialize members */
  channeld->te_trailers = grpc_mdelem_from_strings(mdctx, "te", "trailers");
  channeld->status = grpc_mdelem_from_strings(mdctx, ":status", "200");
  channeld->method = grpc_mdelem_from_strings(mdctx, ":method", "POST");
  channeld->http_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "http");
  channeld->https_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "https");
  channeld->grpc_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "grpc");
  channeld->path_key = grpc_mdstr_from_string(mdctx, ":path");
  channeld->content_type =
      grpc_mdelem_from_strings(mdctx, "content-type", "application/grpc");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  grpc_mdelem_unref(channeld->te_trailers);
  grpc_mdelem_unref(channeld->status);
  grpc_mdelem_unref(channeld->method);
  grpc_mdelem_unref(channeld->http_scheme);
  grpc_mdelem_unref(channeld->https_scheme);
  grpc_mdelem_unref(channeld->grpc_scheme);
  grpc_mdelem_unref(channeld->content_type);
  grpc_mdstr_unref(channeld->path_key);
}

const grpc_channel_filter grpc_http_server_filter = {
    call_op,              channel_op,

    sizeof(call_data),    init_call_elem,    destroy_call_elem,

    sizeof(channel_data), init_channel_elem, destroy_channel_elem,

    "http-server"};
