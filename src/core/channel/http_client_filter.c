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

#include "src/core/channel/http_client_filter.h"
#include <string.h>
#include <grpc/support/log.h>

typedef struct call_data {
  int sent_headers;
} call_data;

typedef struct channel_data {
  grpc_mdelem *te_trailers;
  grpc_mdelem *method;
  grpc_mdelem *scheme;
  grpc_mdelem *content_type;
  grpc_mdelem *status;
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

  ignore_unused(calld);

  switch (op->type) {
    case GRPC_SEND_METADATA:
      if (!calld->sent_headers) {
        /* Send : prefixed headers, which have to be before any application
         * layer headers. */
        calld->sent_headers = 1;
        grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->method));
        grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->scheme));
      }
      grpc_call_next_op(elem, op);
      break;
    case GRPC_SEND_START:
      if (!calld->sent_headers) {
        /* Send : prefixed headers, if we haven't already */
        calld->sent_headers = 1;
        grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->method));
        grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->scheme));
      }
      /* Send non : prefixed headers */
      grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->te_trailers));
      grpc_call_element_send_metadata(elem, grpc_mdelem_ref(channeld->content_type));
      grpc_call_next_op(elem, op);
      break;
    case GRPC_RECV_METADATA:
      if (op->data.metadata == channeld->status) {
        grpc_mdelem_unref(op->data.metadata);
        op->done_cb(op->user_data, GRPC_OP_OK);
      } else if (op->data.metadata->key == channeld->status->key) {
        grpc_mdelem_unref(op->data.metadata);
        op->done_cb(op->user_data, GRPC_OP_OK);
        grpc_call_element_send_cancel(elem);
      } else {
        grpc_call_next_op(elem, op);
      }
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
  calld->sent_headers = 0;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;

  ignore_unused(calld);
  ignore_unused(channeld);
}

static const char *scheme_from_args(const grpc_channel_args *args) {
  unsigned i;
  if (args != NULL) {
    for (i = 0; i < args->num_args; ++i) {
      if (args->args[i].type == GRPC_ARG_STRING &&
          strcmp(args->args[i].key, GRPC_ARG_HTTP2_SCHEME) == 0) {
        return args->args[i].value.string;
      }
    }
  }
  return "http";
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
  channeld->method = grpc_mdelem_from_strings(mdctx, ":method", "POST");
  channeld->scheme =
      grpc_mdelem_from_strings(mdctx, ":scheme", scheme_from_args(args));
  channeld->content_type =
      grpc_mdelem_from_strings(mdctx, "content-type", "application/grpc");
  channeld->status = grpc_mdelem_from_strings(mdctx, ":status", "200");
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  grpc_mdelem_unref(channeld->te_trailers);
  grpc_mdelem_unref(channeld->method);
  grpc_mdelem_unref(channeld->scheme);
  grpc_mdelem_unref(channeld->content_type);
  grpc_mdelem_unref(channeld->status);
}

const grpc_channel_filter grpc_http_client_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "http-client"};
