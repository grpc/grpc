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

#ifndef GRPC_CORE_EXT_LOAD_REPORTING_LOAD_REPORTING_H
#define GRPC_CORE_EXT_LOAD_REPORTING_LOAD_REPORTING_H

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/surface/call.h"

typedef struct grpc_load_reporting_config grpc_load_reporting_config;

/** Call information to be passed to the provided load reporting function upon
 * completion of the call */
typedef struct grpc_load_reporting_call_data {
  const grpc_call_stats *stats;   /**< Stats for the call */
  const char *trailing_md_string; /**< LR trailing metadata info */
} grpc_load_reporting_call_data;

/** Custom function to be called by the load reporting filter. */
typedef void (*grpc_load_reporting_fn)(
    const grpc_load_reporting_call_data *call_data, void *user_data);

/** Register \a fn as the function to be invoked by the load reporting filter.
 * \a fn will be invoked at the beginning and at the end of the call.
 *
 * For the first invocation, \a fn's first argument
 * (grpc_load_reporting_call_data*) will be NULL. \a user_data is always passed
 * as-is. */
grpc_load_reporting_config *grpc_load_reporting_config_create(
    grpc_load_reporting_fn fn, void *user_data);

grpc_load_reporting_config *grpc_load_reporting_config_copy(
    grpc_load_reporting_config *src);

void grpc_load_reporting_config_destroy(grpc_load_reporting_config *lrc);

/** Invoke the function registered by \a grpc_load_reporting_init. */
void grpc_load_reporting_config_call(
    grpc_load_reporting_config *lrc,
    const grpc_load_reporting_call_data *call_data);

/** Return a \a grpc_arg enabling load reporting */
grpc_arg grpc_load_reporting_config_create_arg(grpc_load_reporting_config *lrc);

#endif /* GRPC_CORE_EXT_LOAD_REPORTING_LOAD_REPORTING_H */
