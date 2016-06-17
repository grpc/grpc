/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <string.h>

#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct call_data { const char *trailing_md_string; } call_data;
typedef struct channel_data {
  gpr_mu mu;
  grpc_load_reporting_config *lrc;
} channel_data;

static void invoke_lr_fn_locked(grpc_load_reporting_config *lrc,
                                grpc_load_reporting_call_data *lr_call_data) {
  GPR_TIMER_BEGIN("load_reporting_config_fn", 0);
  grpc_load_reporting_config_call(lrc, lr_call_data);
  GPR_TIMER_END("load_reporting_config_fn", 0);
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  memset(calld, 0, sizeof(call_data));
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_stats *stats, void *ignored) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;

  grpc_load_reporting_call_data lr_call_data = {stats,
                                                calld->trailing_md_string};

  gpr_mu_lock(&chand->mu);
  invoke_lr_fn_locked(chand->lrc, &lr_call_data);
  gpr_mu_unlock(&chand->mu);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);

  channel_data *chand = elem->channel_data;
  memset(chand, 0, sizeof(channel_data));

  gpr_mu_init(&chand->mu);
  for (size_t i = 0; i < args->channel_args->num_args; i++) {
    if (0 == strcmp(args->channel_args->args[i].key,
                    GRPC_ARG_ENABLE_LOAD_REPORTING)) {
      grpc_load_reporting_config *arg_lrc =
          args->channel_args->args[i].value.pointer.p;
      chand->lrc = grpc_load_reporting_config_copy(arg_lrc);
      GPR_ASSERT(chand->lrc != NULL);
      break;
    }
  }
  GPR_ASSERT(chand->lrc != NULL); /* arg actually found */

  gpr_mu_lock(&chand->mu);
  invoke_lr_fn_locked(chand->lrc, NULL);
  gpr_mu_unlock(&chand->mu);
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  gpr_mu_destroy(&chand->mu);
  grpc_load_reporting_config_destroy(chand->lrc);
}

static grpc_mdelem *lr_trailing_md_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;

  if (md->key == GRPC_MDSTR_LOAD_REPORTING) {
    calld->trailing_md_string = gpr_strdup(grpc_mdstr_as_c_string(md->value));
    return NULL;
  }

  return md;
}

static void lr_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("lr_start_transport_stream_op", 0);

  if (op->send_trailing_metadata) {
    grpc_metadata_batch_filter(op->send_trailing_metadata,
                               lr_trailing_md_filter, elem);
  }
  grpc_call_next_op(exec_ctx, elem, op);

  GPR_TIMER_END("lr_start_transport_stream_op", 0);
}

const grpc_channel_filter grpc_load_reporting_filter = {
    lr_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "load_reporting"};
