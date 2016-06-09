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
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"

#define EXPECTED_CONTENT_TYPE "application/grpc"
#define EXPECTED_CONTENT_TYPE_LENGTH sizeof(EXPECTED_CONTENT_TYPE) - 1

typedef struct call_data {
  grpc_linked_mdelem method;
  grpc_linked_mdelem scheme;
  grpc_linked_mdelem authority;
  grpc_linked_mdelem te_trailers;
  grpc_linked_mdelem content_type;
  grpc_linked_mdelem user_agent;

  grpc_metadata_batch *recv_initial_metadata;

  /** Closure to call when finished with the hc_on_recv hook */
  grpc_closure *on_done_recv;
  /** Receive closures are chained: we inject this closure as the on_done_recv
      up-call on transport_op, and remember to call our on_done_recv member
      after handling it. */
  grpc_closure hc_on_recv;
} call_data;

typedef struct channel_data {
  grpc_mdelem *static_scheme;
  grpc_mdelem *user_agent;
} channel_data;

typedef struct {
  grpc_call_element *elem;
  grpc_exec_ctx *exec_ctx;
} client_recv_filter_args;

static grpc_mdelem *client_recv_filter(void *user_data, grpc_mdelem *md) {
  client_recv_filter_args *a = user_data;
  if (md == GRPC_MDELEM_STATUS_200) {
    return NULL;
  } else if (md->key == GRPC_MDSTR_STATUS) {
    grpc_call_element_send_cancel(a->exec_ctx, a->elem);
    return NULL;
  } else if (md == GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC) {
    return NULL;
  } else if (md->key == GRPC_MDSTR_CONTENT_TYPE) {
    const char *value_str = grpc_mdstr_as_c_string(md->value);
    if (strncmp(value_str, EXPECTED_CONTENT_TYPE,
                EXPECTED_CONTENT_TYPE_LENGTH) == 0 &&
        (value_str[EXPECTED_CONTENT_TYPE_LENGTH] == '+' ||
         value_str[EXPECTED_CONTENT_TYPE_LENGTH] == ';')) {
      /* Although the C implementation doesn't (currently) generate them,
         any custom +-suffix is explicitly valid. */
      /* TODO(klempner): We should consider preallocating common values such
         as +proto or +json, or at least stashing them if we see them. */
      /* TODO(klempner): Should we be surfacing this to application code? */
    } else {
      /* TODO(klempner): We're currently allowing this, but we shouldn't
         see it without a proxy so log for now. */
      gpr_log(GPR_INFO, "Unexpected content-type '%s'", value_str);
    }
    return NULL;
  }
  return md;
}

static void hc_on_recv(grpc_exec_ctx *exec_ctx, void *user_data, bool success) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  client_recv_filter_args a;
  a.elem = elem;
  a.exec_ctx = exec_ctx;
  grpc_metadata_batch_filter(calld->recv_initial_metadata, client_recv_filter,
                             &a);
  calld->on_done_recv->cb(exec_ctx, calld->on_done_recv->cb_arg, success);
}

static grpc_mdelem *client_strip_filter(void *user_data, grpc_mdelem *md) {
  /* eat the things we'd like to set ourselves */
  if (md->key == GRPC_MDSTR_METHOD) return NULL;
  if (md->key == GRPC_MDSTR_SCHEME) return NULL;
  if (md->key == GRPC_MDSTR_TE) return NULL;
  if (md->key == GRPC_MDSTR_CONTENT_TYPE) return NULL;
  if (md->key == GRPC_MDSTR_USER_AGENT) return NULL;
  return md;
}

static void hc_mutate_op(grpc_call_element *elem,
                         grpc_transport_stream_op *op) {
  /* grab pointers to our data from the call element */
  call_data *calld = elem->call_data;
  channel_data *channeld = elem->channel_data;
  if (op->send_initial_metadata != NULL) {
    grpc_metadata_batch_filter(op->send_initial_metadata, client_strip_filter,
                               elem);
    /* Send : prefixed headers, which have to be before any application
       layer headers. */
    grpc_metadata_batch_add_head(
        op->send_initial_metadata, &calld->method,
        op->send_initial_metadata_flags &
                GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST
            ? GRPC_MDELEM_METHOD_PUT
            : GRPC_MDELEM_METHOD_POST);
    grpc_metadata_batch_add_head(op->send_initial_metadata, &calld->scheme,
                                 channeld->static_scheme);
    grpc_metadata_batch_add_tail(op->send_initial_metadata, &calld->te_trailers,
                                 GRPC_MDELEM_TE_TRAILERS);
    grpc_metadata_batch_add_tail(
        op->send_initial_metadata, &calld->content_type,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC);
    grpc_metadata_batch_add_tail(op->send_initial_metadata, &calld->user_agent,
                                 GRPC_MDELEM_REF(channeld->user_agent));
  }

  if (op->recv_initial_metadata != NULL) {
    /* substitute our callback for the higher callback */
    calld->recv_initial_metadata = op->recv_initial_metadata;
    calld->on_done_recv = op->recv_initial_metadata_ready;
    op->recv_initial_metadata_ready = &calld->hc_on_recv;
  }
}

static void hc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("hc_start_transport_op", 0);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  hc_mutate_op(elem, op);
  GPR_TIMER_END("hc_start_transport_op", 0);
  grpc_call_next_op(exec_ctx, elem, op);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  calld->on_done_recv = NULL;
  grpc_closure_init(&calld->hc_on_recv, hc_on_recv, elem);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_stats *stats, void *ignored) {}

static grpc_mdelem *scheme_from_args(const grpc_channel_args *args) {
  unsigned i;
  size_t j;
  grpc_mdelem *valid_schemes[] = {GRPC_MDELEM_SCHEME_HTTP,
                                  GRPC_MDELEM_SCHEME_HTTPS};
  if (args != NULL) {
    for (i = 0; i < args->num_args; ++i) {
      if (args->args[i].type == GRPC_ARG_STRING &&
          strcmp(args->args[i].key, GRPC_ARG_HTTP2_SCHEME) == 0) {
        for (j = 0; j < GPR_ARRAY_SIZE(valid_schemes); j++) {
          if (0 == strcmp(grpc_mdstr_as_c_string(valid_schemes[j]->value),
                          args->args[i].value.string)) {
            return valid_schemes[j];
          }
        }
      }
    }
  }
  return GRPC_MDELEM_SCHEME_HTTP;
}

static grpc_mdstr *user_agent_from_args(const grpc_channel_args *args) {
  gpr_strvec v;
  size_t i;
  int is_first = 1;
  char *tmp;
  grpc_mdstr *result;

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

  gpr_asprintf(&tmp, "%sgrpc-c/%s (%s)", is_first ? "" : " ",
               grpc_version_string(), GPR_PLATFORM_STRING);
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
  result = grpc_mdstr_from_string(tmp);
  gpr_free(tmp);

  return result;
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(!args->is_last);
  chand->static_scheme = scheme_from_args(args->channel_args);
  chand->user_agent = grpc_mdelem_from_metadata_strings(
      GRPC_MDSTR_USER_AGENT, user_agent_from_args(args->channel_args));
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  GRPC_MDELEM_UNREF(chand->user_agent);
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
    "http-client"};
