/*
 *
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

#include "src/core/channel/http_server_filter.h"

#include <string.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/profiling/timers.h"

typedef struct call_data {
  gpr_uint8 got_initial_metadata;
  gpr_uint8 seen_path;
  gpr_uint8 seen_post;
  gpr_uint8 sent_status;
  gpr_uint8 seen_scheme;
  gpr_uint8 seen_te_trailers;
  gpr_uint8 seen_authority;
  grpc_linked_mdelem status;
  grpc_linked_mdelem content_type;

  grpc_stream_op_buffer *recv_ops;
  /** Closure to call when finished with the hs_on_recv hook */
  grpc_closure *on_done_recv;
  /** Receive closures are chained: we inject this closure as the on_done_recv
      up-call on transport_op, and remember to call our on_done_recv member
      after handling it. */
  grpc_closure hs_on_recv;
} call_data;

typedef struct channel_data {
  grpc_mdelem *te_trailers;
  grpc_mdelem *method_post;
  grpc_mdelem *http_scheme;
  grpc_mdelem *https_scheme;
  /* TODO(klempner): Remove this once we stop using it */
  grpc_mdelem *grpc_scheme;
  grpc_mdelem *content_type;
  grpc_mdelem *status_ok;
  grpc_mdelem *status_not_found;
  grpc_mdstr *path_key;
  grpc_mdstr *authority_key;
  grpc_mdstr *host_key;

  grpc_mdctx *mdctx;
} channel_data;

typedef struct {
  grpc_call_element *elem;
  grpc_exec_ctx *exec_ctx;
} server_filter_args;

static grpc_mdelem *server_filter(void *user_data, grpc_mdelem *md) {
  server_filter_args *a = user_data;
  grpc_call_element *elem = a->elem;
  channel_data *channeld = elem->channel_data;
  call_data *calld = elem->call_data;

  /* Check if it is one of the headers we care about. */
  if (md == channeld->te_trailers || md == channeld->method_post ||
      md == channeld->http_scheme || md == channeld->https_scheme ||
      md == channeld->grpc_scheme || md == channeld->content_type) {
    /* swallow it */
    if (md == channeld->method_post) {
      calld->seen_post = 1;
    } else if (md->key == channeld->http_scheme->key) {
      calld->seen_scheme = 1;
    } else if (md == channeld->te_trailers) {
      calld->seen_te_trailers = 1;
    }
    /* TODO(klempner): Track that we've seen all the headers we should
       require */
    return NULL;
  } else if (md->key == channeld->content_type->key) {
    if (strncmp(grpc_mdstr_as_c_string(md->value), "application/grpc+", 17) ==
        0) {
      /* Although the C implementation doesn't (currently) generate them,
         any custom +-suffix is explicitly valid. */
      /* TODO(klempner): We should consider preallocating common values such
         as +proto or +json, or at least stashing them if we see them. */
      /* TODO(klempner): Should we be surfacing this to application code? */
    } else {
      /* TODO(klempner): We're currently allowing this, but we shouldn't
         see it without a proxy so log for now. */
      gpr_log(GPR_INFO, "Unexpected content-type %s",
              channeld->content_type->key);
    }
    return NULL;
  } else if (md->key == channeld->te_trailers->key ||
             md->key == channeld->method_post->key ||
             md->key == channeld->http_scheme->key) {
    gpr_log(GPR_ERROR, "Invalid %s: header: '%s'",
            grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value));
    /* swallow it and error everything out. */
    /* TODO(klempner): We ought to generate more descriptive error messages
       on the wire here. */
    grpc_call_element_send_cancel(a->exec_ctx, elem);
    return NULL;
  } else if (md->key == channeld->path_key) {
    if (calld->seen_path) {
      gpr_log(GPR_ERROR, "Received :path twice");
      return NULL;
    }
    calld->seen_path = 1;
    return md;
  } else if (md->key == channeld->authority_key) {
    calld->seen_authority = 1;
    return md;
  } else if (md->key == channeld->host_key) {
    /* translate host to :authority since :authority may be
       omitted */
    grpc_mdelem *authority = grpc_mdelem_from_metadata_strings(
        channeld->mdctx, GRPC_MDSTR_REF(channeld->authority_key),
        GRPC_MDSTR_REF(md->value));
    GRPC_MDELEM_UNREF(md);
    calld->seen_authority = 1;
    return authority;
  } else {
    return md;
  }
}

static void hs_on_recv(grpc_exec_ctx *exec_ctx, void *user_data, int success) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  if (success) {
    size_t i;
    size_t nops = calld->recv_ops->nops;
    grpc_stream_op *ops = calld->recv_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *op = &ops[i];
      server_filter_args a;
      if (op->type != GRPC_OP_METADATA) continue;
      calld->got_initial_metadata = 1;
      a.elem = elem;
      a.exec_ctx = exec_ctx;
      grpc_metadata_batch_filter(&op->data.metadata, server_filter, &a);
      /* Have we seen the required http2 transport headers?
         (:method, :scheme, content-type, with :path and :authority covered
         at the channel level right now) */
      if (calld->seen_post && calld->seen_scheme && calld->seen_te_trailers &&
          calld->seen_path && calld->seen_authority) {
        /* do nothing */
      } else {
        if (!calld->seen_path) {
          gpr_log(GPR_ERROR, "Missing :path header");
        }
        if (!calld->seen_authority) {
          gpr_log(GPR_ERROR, "Missing :authority header");
        }
        if (!calld->seen_post) {
          gpr_log(GPR_ERROR, "Missing :method header");
        }
        if (!calld->seen_scheme) {
          gpr_log(GPR_ERROR, "Missing :scheme header");
        }
        if (!calld->seen_te_trailers) {
          gpr_log(GPR_ERROR, "Missing te trailers header");
        }
        /* Error this call out */
        success = 0;
        grpc_call_element_send_cancel(exec_ctx, elem);
      }
    }
  }
  calld->on_done_recv->cb(exec_ctx, calld->on_done_recv->cb_arg, success);
}

static void hs_mutate_op(grpc_call_element *elem,
                         grpc_transport_stream_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  size_t i;

  if (op->send_ops && !calld->sent_status) {
    size_t nops = op->send_ops->nops;
    grpc_stream_op *ops = op->send_ops->ops;
    for (i = 0; i < nops; i++) {
      grpc_stream_op *stream_op = &ops[i];
      if (stream_op->type != GRPC_OP_METADATA) continue;
      calld->sent_status = 1;
      grpc_metadata_batch_add_head(&stream_op->data.metadata, &calld->status,
                                   GRPC_MDELEM_REF(channeld->status_ok));
      grpc_metadata_batch_add_tail(&stream_op->data.metadata,
                                   &calld->content_type,
                                   GRPC_MDELEM_REF(channeld->content_type));
      break;
    }
  }

  if (op->recv_ops && !calld->got_initial_metadata) {
    /* substitute our callback for the higher callback */
    calld->recv_ops = op->recv_ops;
    calld->on_done_recv = op->on_done_recv;
    op->on_done_recv = &calld->hs_on_recv;
  }
}

static void hs_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  GPR_TIMER_BEGIN("hs_start_transport_op", 0);
  hs_mutate_op(elem, op);
  grpc_call_next_op(exec_ctx, elem, op);
  GPR_TIMER_END("hs_start_transport_op", 0);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  /* initialize members */
  memset(calld, 0, sizeof(*calld));
  grpc_closure_init(&calld->hs_on_recv, hs_on_recv, elem);
  if (initial_op) hs_mutate_op(elem, initial_op);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem, grpc_channel *master,
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
  channeld->status_ok = grpc_mdelem_from_strings(mdctx, ":status", "200");
  channeld->status_not_found =
      grpc_mdelem_from_strings(mdctx, ":status", "404");
  channeld->method_post = grpc_mdelem_from_strings(mdctx, ":method", "POST");
  channeld->http_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "http");
  channeld->https_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "https");
  channeld->grpc_scheme = grpc_mdelem_from_strings(mdctx, ":scheme", "grpc");
  channeld->path_key = grpc_mdstr_from_string(mdctx, ":path");
  channeld->authority_key = grpc_mdstr_from_string(mdctx, ":authority");
  channeld->host_key = grpc_mdstr_from_string(mdctx, "host");
  channeld->content_type =
      grpc_mdelem_from_strings(mdctx, "content-type", "application/grpc");

  channeld->mdctx = mdctx;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  /* grab pointers to our data from the channel element */
  channel_data *channeld = elem->channel_data;

  GRPC_MDELEM_UNREF(channeld->te_trailers);
  GRPC_MDELEM_UNREF(channeld->status_ok);
  GRPC_MDELEM_UNREF(channeld->status_not_found);
  GRPC_MDELEM_UNREF(channeld->method_post);
  GRPC_MDELEM_UNREF(channeld->http_scheme);
  GRPC_MDELEM_UNREF(channeld->https_scheme);
  GRPC_MDELEM_UNREF(channeld->grpc_scheme);
  GRPC_MDELEM_UNREF(channeld->content_type);
  GRPC_MDSTR_UNREF(channeld->path_key);
  GRPC_MDSTR_UNREF(channeld->authority_key);
  GRPC_MDSTR_UNREF(channeld->host_key);
}

const grpc_channel_filter grpc_http_server_filter = {
    hs_start_transport_op, grpc_channel_next_op, sizeof(call_data),
    init_call_elem,        destroy_call_elem,    sizeof(channel_data),
    init_channel_elem,     destroy_channel_elem, grpc_call_next_get_peer,
    "http-server"};
