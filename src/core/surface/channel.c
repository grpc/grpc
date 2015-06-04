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

#include "src/core/surface/channel.h"

#include <stdlib.h>
#include <string.h>

#include "src/core/iomgr/iomgr.h"
#include "src/core/surface/call.h"
#include "src/core/surface/client.h"
#include "src/core/surface/init.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct registered_call {
  grpc_mdelem *path;
  grpc_mdelem *authority;
  struct registered_call *next;
} registered_call;

struct grpc_channel {
  int is_client;
  gpr_refcount refs;
  gpr_uint32 max_message_length;
  grpc_mdctx *metadata_context;
  grpc_mdstr *grpc_status_string;
  grpc_mdstr *grpc_message_string;
  grpc_mdstr *path_string;
  grpc_mdstr *authority_string;

  gpr_mu registered_call_mu;
  registered_call *registered_calls;
  grpc_iomgr_closure destroy_closure;
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack *)((c) + 1))
#define CHANNEL_FROM_CHANNEL_STACK(channel_stack) \
  (((grpc_channel *)(channel_stack)) - 1)
#define CHANNEL_FROM_TOP_ELEM(top_elem) \
  CHANNEL_FROM_CHANNEL_STACK(grpc_channel_stack_from_top_element(top_elem))

/* the protobuf library will (by default) start warning at 100megs */
#define DEFAULT_MAX_MESSAGE_LENGTH (100 * 1024 * 1024)

grpc_channel *grpc_channel_create_from_filters(
    const grpc_channel_filter **filters, size_t num_filters,
    const grpc_channel_args *args, grpc_mdctx *mdctx, int is_client) {
  size_t i;
  size_t size =
      sizeof(grpc_channel) + grpc_channel_stack_size(filters, num_filters);
  grpc_channel *channel = gpr_malloc(size);
  GPR_ASSERT(grpc_is_initialized() && "call grpc_init()");
  channel->is_client = is_client;
  /* decremented by grpc_channel_destroy, and grpc_client_channel_closed if
   * is_client */
  gpr_ref_init(&channel->refs, 1 + is_client);
  channel->metadata_context = mdctx;
  channel->grpc_status_string = grpc_mdstr_from_string(mdctx, "grpc-status");
  channel->grpc_message_string = grpc_mdstr_from_string(mdctx, "grpc-message");
  channel->path_string = grpc_mdstr_from_string(mdctx, ":path");
  channel->authority_string = grpc_mdstr_from_string(mdctx, ":authority");
  grpc_channel_stack_init(filters, num_filters, args, channel->metadata_context,
                          CHANNEL_STACK_FROM_CHANNEL(channel));
  gpr_mu_init(&channel->registered_call_mu);
  channel->registered_calls = NULL;

  channel->max_message_length = DEFAULT_MAX_MESSAGE_LENGTH;
  if (args) {
    for (i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_MAX_MESSAGE_LENGTH)) {
        if (args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s ignored: it must be an integer",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else if (args->args[i].value.integer < 0) {
          gpr_log(GPR_ERROR, "%s ignored: it must be >= 0",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else {
          channel->max_message_length = args->args[i].value.integer;
        }
      }
    }
  }

  return channel;
}

static grpc_call *grpc_channel_create_call_internal(
    grpc_channel *channel, grpc_completion_queue *cq, grpc_mdelem *path_mdelem,
    grpc_mdelem *authority_mdelem, gpr_timespec deadline) {
  grpc_mdelem *send_metadata[2];

  GPR_ASSERT(channel->is_client);

  send_metadata[0] = path_mdelem;
  send_metadata[1] = authority_mdelem;

  return grpc_call_create(channel, cq, NULL, send_metadata,
                          GPR_ARRAY_SIZE(send_metadata), deadline);
}

grpc_call *grpc_channel_create_call(grpc_channel *channel,
                                    grpc_completion_queue *cq,
                                    const char *method, const char *host,
                                    gpr_timespec deadline) {
  return grpc_channel_create_call_internal(
      channel, cq,
      grpc_mdelem_from_metadata_strings(
          channel->metadata_context, grpc_mdstr_ref(channel->path_string),
          grpc_mdstr_from_string(channel->metadata_context, method)),
      grpc_mdelem_from_metadata_strings(
          channel->metadata_context, grpc_mdstr_ref(channel->authority_string),
          grpc_mdstr_from_string(channel->metadata_context, host)),
      deadline);
}

void *grpc_channel_register_call(grpc_channel *channel, const char *method,
                                 const char *host) {
  registered_call *rc = gpr_malloc(sizeof(registered_call));
  rc->path = grpc_mdelem_from_metadata_strings(
      channel->metadata_context, grpc_mdstr_ref(channel->path_string),
      grpc_mdstr_from_string(channel->metadata_context, method));
  rc->authority = grpc_mdelem_from_metadata_strings(
      channel->metadata_context, grpc_mdstr_ref(channel->authority_string),
      grpc_mdstr_from_string(channel->metadata_context, host));
  gpr_mu_lock(&channel->registered_call_mu);
  rc->next = channel->registered_calls;
  channel->registered_calls = rc;
  gpr_mu_unlock(&channel->registered_call_mu);
  return rc;
}

grpc_call *grpc_channel_create_registered_call(
    grpc_channel *channel, grpc_completion_queue *completion_queue,
    void *registered_call_handle, gpr_timespec deadline) {
  registered_call *rc = registered_call_handle;
  return grpc_channel_create_call_internal(
      channel, completion_queue, grpc_mdelem_ref(rc->path),
      grpc_mdelem_ref(rc->authority), deadline);
}

void grpc_channel_internal_ref(grpc_channel *channel) {
  gpr_ref(&channel->refs);
}

static void destroy_channel(void *p, int ok) {
  grpc_channel *channel = p;
  grpc_channel_stack_destroy(CHANNEL_STACK_FROM_CHANNEL(channel));
  grpc_mdstr_unref(channel->grpc_status_string);
  grpc_mdstr_unref(channel->grpc_message_string);
  grpc_mdstr_unref(channel->path_string);
  grpc_mdstr_unref(channel->authority_string);
  while (channel->registered_calls) {
    registered_call *rc = channel->registered_calls;
    channel->registered_calls = rc->next;
    grpc_mdelem_unref(rc->path);
    grpc_mdelem_unref(rc->authority);
    gpr_free(rc);
  }
  grpc_mdctx_unref(channel->metadata_context);
  gpr_mu_destroy(&channel->registered_call_mu);
  gpr_free(channel);
}

void grpc_channel_internal_unref(grpc_channel *channel) {
  if (gpr_unref(&channel->refs)) {
    channel->destroy_closure.cb = destroy_channel;
    channel->destroy_closure.cb_arg = channel;
    grpc_iomgr_add_callback(&channel->destroy_closure);
  }
}

void grpc_channel_destroy(grpc_channel *channel) {
  grpc_channel_op op;
  grpc_channel_element *elem;

  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);

  op.type = GRPC_CHANNEL_GOAWAY;
  op.dir = GRPC_CALL_DOWN;
  op.data.goaway.status = GRPC_STATUS_OK;
  op.data.goaway.message = gpr_slice_from_copied_string("Client disconnect");
  elem->filter->channel_op(elem, NULL, &op);

  op.type = GRPC_CHANNEL_DISCONNECT;
  op.dir = GRPC_CALL_DOWN;
  elem->filter->channel_op(elem, NULL, &op);

  grpc_channel_internal_unref(channel);
}

void grpc_client_channel_closed(grpc_channel_element *elem) {
  grpc_channel_internal_unref(CHANNEL_FROM_TOP_ELEM(elem));
}

grpc_channel_stack *grpc_channel_get_channel_stack(grpc_channel *channel) {
  return CHANNEL_STACK_FROM_CHANNEL(channel);
}

grpc_mdctx *grpc_channel_get_metadata_context(grpc_channel *channel) {
  return channel->metadata_context;
}

grpc_mdstr *grpc_channel_get_status_string(grpc_channel *channel) {
  return channel->grpc_status_string;
}

grpc_mdstr *grpc_channel_get_message_string(grpc_channel *channel) {
  return channel->grpc_message_string;
}

gpr_uint32 grpc_channel_get_max_message_length(grpc_channel *channel) {
  return channel->max_message_length;
}
