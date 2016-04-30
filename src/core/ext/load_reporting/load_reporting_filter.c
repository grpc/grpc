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

#include <string.h>
#include <grpc/support/log.h>

#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/profiling/timers.h"

typedef struct call_data { void *dummy; } call_data;
typedef struct channel_data { grpc_load_reporting_data *lrd; } channel_data;

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_stats *stats, void *ignored) {
  channel_data *chand = elem->channel_data;
  GPR_TIMER_BEGIN("load_reporting_filter", 0);
  grpc_load_reporting_call(chand->lrd, stats);
  GPR_TIMER_END("load_reporting_filter", 0);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);

  channel_data *chand = elem->channel_data;
  memset(chand, 0, sizeof(channel_data));

  for (size_t i = 0; i < args->channel_args->num_args; i++) {
    if (0 == strcmp(args->channel_args->args[i].key, GRPC_ARG_ENABLE_LOAD_REPORTING)) {
      chand->lrd = args->channel_args->args[i].value.pointer.p;
      GPR_ASSERT(chand->lrd != NULL);
    }
  }
  GPR_ASSERT(chand->lrd != NULL); /* arg actually found */

}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  grpc_load_reporting_destroy(chand->lrd);
}

const grpc_channel_filter grpc_load_reporting_filter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "load_reporting"};
