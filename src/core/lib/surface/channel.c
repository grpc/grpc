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

#include "src/core/lib/surface/channel.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

/** Cache grpc-status: X mdelems for X = 0..NUM_CACHED_STATUS_ELEMS.
 *  Avoids needing to take a metadata context lock for sending status
 *  if the status code is <= NUM_CACHED_STATUS_ELEMS.
 *  Sized to allow the most commonly used codes to fit in
 *  (OK, Cancelled, Unknown). */
#define NUM_CACHED_STATUS_ELEMS 3

typedef struct registered_call {
  grpc_mdelem *path;
  grpc_mdelem *authority;
  struct registered_call *next;
} registered_call;

struct grpc_channel {
  int is_client;
  uint32_t max_message_length;
  grpc_compression_options compression_options;
  grpc_mdelem *default_authority;

  gpr_mu registered_call_mu;
  registered_call *registered_calls;

  char *target;
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack *)((c) + 1))
#define CHANNEL_FROM_CHANNEL_STACK(channel_stack) \
  (((grpc_channel *)(channel_stack)) - 1)
#define CHANNEL_FROM_TOP_ELEM(top_elem) \
  CHANNEL_FROM_CHANNEL_STACK(grpc_channel_stack_from_top_element(top_elem))

/* the protobuf library will (by default) start warning at 100megs */
#define DEFAULT_MAX_MESSAGE_LENGTH (4 * 1024 * 1024)

static void destroy_channel(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error);

grpc_channel *grpc_channel_create(grpc_exec_ctx *exec_ctx, const char *target,
                                  const grpc_channel_args *input_args,
                                  grpc_channel_stack_type channel_stack_type,
                                  grpc_transport *optional_transport) {
  bool is_client = grpc_channel_stack_type_is_client(channel_stack_type);

  grpc_channel_stack_builder *builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(builder, input_args);
  grpc_channel_stack_builder_set_target(builder, target);
  grpc_channel_stack_builder_set_transport(builder, optional_transport);
  grpc_channel *channel;
  grpc_channel_args *args;
  if (!grpc_channel_init_create_stack(exec_ctx, builder, channel_stack_type)) {
    grpc_channel_stack_builder_destroy(builder);
    return NULL;
  } else {
    args = grpc_channel_args_copy(
        grpc_channel_stack_builder_get_channel_arguments(builder));
    channel = grpc_channel_stack_builder_finish(
        exec_ctx, builder, sizeof(grpc_channel), 1, destroy_channel, NULL);
  }

  memset(channel, 0, sizeof(*channel));
  channel->target = gpr_strdup(target);
  channel->is_client = is_client;
  gpr_mu_init(&channel->registered_call_mu);
  channel->registered_calls = NULL;

  channel->max_message_length = DEFAULT_MAX_MESSAGE_LENGTH;
  grpc_compression_options_init(&channel->compression_options);
  if (args) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_MAX_MESSAGE_LENGTH)) {
        if (args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s ignored: it must be an integer",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else if (args->args[i].value.integer < 0) {
          gpr_log(GPR_ERROR, "%s ignored: it must be >= 0",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else {
          channel->max_message_length = (uint32_t)args->args[i].value.integer;
        }
      } else if (0 == strcmp(args->args[i].key, GRPC_ARG_DEFAULT_AUTHORITY)) {
        if (args->args[i].type != GRPC_ARG_STRING) {
          gpr_log(GPR_ERROR, "%s ignored: it must be a string",
                  GRPC_ARG_DEFAULT_AUTHORITY);
        } else {
          if (channel->default_authority) {
            /* setting this takes precedence over anything else */
            GRPC_MDELEM_UNREF(channel->default_authority);
          }
          channel->default_authority = grpc_mdelem_from_strings(
              ":authority", args->args[i].value.string);
        }
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)) {
        if (args->args[i].type != GRPC_ARG_STRING) {
          gpr_log(GPR_ERROR, "%s ignored: it must be a string",
                  GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
        } else {
          if (channel->default_authority) {
            /* other ways of setting this (notably ssl) take precedence */
            gpr_log(GPR_ERROR,
                    "%s ignored: default host already set some other way",
                    GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
          } else {
            channel->default_authority = grpc_mdelem_from_strings(
                ":authority", args->args[i].value.string);
          }
        }
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL)) {
        channel->compression_options.default_level.is_set = true;
        GPR_ASSERT(args->args[i].value.integer >= 0 &&
                   args->args[i].value.integer < GRPC_COMPRESS_LEVEL_COUNT);
        channel->compression_options.default_level.level =
            (grpc_compression_level)args->args[i].value.integer;
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM)) {
        channel->compression_options.default_algorithm.is_set = true;
        GPR_ASSERT(args->args[i].value.integer >= 0 &&
                   args->args[i].value.integer <
                       GRPC_COMPRESS_ALGORITHMS_COUNT);
        channel->compression_options.default_algorithm.algorithm =
            (grpc_compression_algorithm)args->args[i].value.integer;
      } else if (0 ==
                 strcmp(args->args[i].key,
                        GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET)) {
        channel->compression_options.enabled_algorithms_bitset =
            (uint32_t)args->args[i].value.integer |
            0x1; /* always support no compression */
      }
    }
    grpc_channel_args_destroy(args);
  }

  return channel;
}

char *grpc_channel_get_target(grpc_channel *channel) {
  GRPC_API_TRACE("grpc_channel_get_target(channel=%p)", 1, (channel));
  return gpr_strdup(channel->target);
}

static grpc_call *grpc_channel_create_call_internal(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_completion_queue *cq, grpc_pollset_set *pollset_set_alternative,
    grpc_mdelem *path_mdelem, grpc_mdelem *authority_mdelem,
    gpr_timespec deadline) {
  grpc_mdelem *send_metadata[2];
  size_t num_metadata = 0;

  GPR_ASSERT(channel->is_client);
  GPR_ASSERT(!(cq != NULL && pollset_set_alternative != NULL));

  send_metadata[num_metadata++] = path_mdelem;
  if (authority_mdelem != NULL) {
    send_metadata[num_metadata++] = authority_mdelem;
  } else if (channel->default_authority != NULL) {
    send_metadata[num_metadata++] = GRPC_MDELEM_REF(channel->default_authority);
  }

  return grpc_call_create(channel, parent_call, propagation_mask, cq,
                          pollset_set_alternative, NULL, send_metadata,
                          num_metadata, deadline);
}

grpc_call *grpc_channel_create_call(grpc_channel *channel,
                                    grpc_call *parent_call,
                                    uint32_t propagation_mask,
                                    grpc_completion_queue *cq,
                                    const char *method, const char *host,
                                    gpr_timespec deadline, void *reserved) {
  GRPC_API_TRACE(
      "grpc_channel_create_call("
      "channel=%p, parent_call=%p, propagation_mask=%x, cq=%p, method=%s, "
      "host=%s, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      10,
      (channel, parent_call, (unsigned)propagation_mask, cq, method, host,
       deadline.tv_sec, deadline.tv_nsec, (int)deadline.clock_type, reserved));
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, cq, NULL,
      grpc_mdelem_from_metadata_strings(GRPC_MDSTR_PATH,
                                        grpc_mdstr_from_string(method)),
      host ? grpc_mdelem_from_metadata_strings(GRPC_MDSTR_AUTHORITY,
                                               grpc_mdstr_from_string(host))
           : NULL,
      deadline);
}

grpc_call *grpc_channel_create_pollset_set_call(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_pollset_set *pollset_set, const char *method, const char *host,
    gpr_timespec deadline, void *reserved) {
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, NULL, pollset_set,
      grpc_mdelem_from_metadata_strings(GRPC_MDSTR_PATH,
                                        grpc_mdstr_from_string(method)),
      host ? grpc_mdelem_from_metadata_strings(GRPC_MDSTR_AUTHORITY,
                                               grpc_mdstr_from_string(host))
           : NULL,
      deadline);
}

void *grpc_channel_register_call(grpc_channel *channel, const char *method,
                                 const char *host, void *reserved) {
  registered_call *rc = gpr_malloc(sizeof(registered_call));
  GRPC_API_TRACE(
      "grpc_channel_register_call(channel=%p, method=%s, host=%s, reserved=%p)",
      4, (channel, method, host, reserved));
  GPR_ASSERT(!reserved);
  rc->path = grpc_mdelem_from_metadata_strings(GRPC_MDSTR_PATH,
                                               grpc_mdstr_from_string(method));
  rc->authority = host ? grpc_mdelem_from_metadata_strings(
                             GRPC_MDSTR_AUTHORITY, grpc_mdstr_from_string(host))
                       : NULL;
  gpr_mu_lock(&channel->registered_call_mu);
  rc->next = channel->registered_calls;
  channel->registered_calls = rc;
  gpr_mu_unlock(&channel->registered_call_mu);
  return rc;
}

grpc_call *grpc_channel_create_registered_call(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_completion_queue *completion_queue, void *registered_call_handle,
    gpr_timespec deadline, void *reserved) {
  registered_call *rc = registered_call_handle;
  GRPC_API_TRACE(
      "grpc_channel_create_registered_call("
      "channel=%p, parent_call=%p, propagation_mask=%x, completion_queue=%p, "
      "registered_call_handle=%p, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      9, (channel, parent_call, (unsigned)propagation_mask, completion_queue,
          registered_call_handle, deadline.tv_sec, deadline.tv_nsec,
          (int)deadline.clock_type, reserved));
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, completion_queue, NULL,
      GRPC_MDELEM_REF(rc->path),
      rc->authority ? GRPC_MDELEM_REF(rc->authority) : NULL, deadline);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define REF_REASON reason
#define REF_ARG , const char *reason
#else
#define REF_REASON ""
#define REF_ARG
#endif
void grpc_channel_internal_ref(grpc_channel *c REF_ARG) {
  GRPC_CHANNEL_STACK_REF(CHANNEL_STACK_FROM_CHANNEL(c), REF_REASON);
}

void grpc_channel_internal_unref(grpc_exec_ctx *exec_ctx,
                                 grpc_channel *c REF_ARG) {
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, CHANNEL_STACK_FROM_CHANNEL(c), REF_REASON);
}

static void destroy_channel(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error) {
  grpc_channel *channel = arg;
  grpc_channel_stack_destroy(exec_ctx, CHANNEL_STACK_FROM_CHANNEL(channel));
  while (channel->registered_calls) {
    registered_call *rc = channel->registered_calls;
    channel->registered_calls = rc->next;
    GRPC_MDELEM_UNREF(rc->path);
    if (rc->authority) {
      GRPC_MDELEM_UNREF(rc->authority);
    }
    gpr_free(rc);
  }
  if (channel->default_authority != NULL) {
    GRPC_MDELEM_UNREF(channel->default_authority);
  }
  gpr_mu_destroy(&channel->registered_call_mu);
  gpr_free(channel->target);
  gpr_free(channel);
}

void grpc_channel_destroy(grpc_channel *channel) {
  grpc_transport_op op;
  grpc_channel_element *elem;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE("grpc_channel_destroy(channel=%p)", 1, (channel));
  memset(&op, 0, sizeof(op));
  op.disconnect_with_error = GRPC_ERROR_CREATE("Channel Destroyed");
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->start_transport_op(&exec_ctx, elem, &op);

  GRPC_CHANNEL_INTERNAL_UNREF(&exec_ctx, channel, "channel");

  grpc_exec_ctx_finish(&exec_ctx);
}

grpc_channel_stack *grpc_channel_get_channel_stack(grpc_channel *channel) {
  return CHANNEL_STACK_FROM_CHANNEL(channel);
}

grpc_compression_options grpc_channel_compression_options(
    const grpc_channel *channel) {
  return channel->compression_options;
}

grpc_mdelem *grpc_channel_get_reffed_status_elem(grpc_channel *channel, int i) {
  char tmp[GPR_LTOA_MIN_BUFSIZE];
  switch (i) {
    case 0:
      return GRPC_MDELEM_GRPC_STATUS_0;
    case 1:
      return GRPC_MDELEM_GRPC_STATUS_1;
    case 2:
      return GRPC_MDELEM_GRPC_STATUS_2;
  }
  gpr_ltoa(i, tmp);
  return grpc_mdelem_from_metadata_strings(GRPC_MDSTR_GRPC_STATUS,
                                           grpc_mdstr_from_string(tmp));
}

uint32_t grpc_channel_get_max_message_length(grpc_channel *channel) {
  return channel->max_message_length;
}
