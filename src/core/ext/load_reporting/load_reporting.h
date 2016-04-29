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

typedef struct grpc_load_reporting_data grpc_load_reporting_data;

/** Custom function to be called by the load reporting filter.
 *
 * The \a data pointer is the same as the one passed to \a
 * grpc_load_reporting_init. \a stats are the final per-call statistics gathered
 * by the gRPC runtime. */
typedef void (*grpc_load_reporting_fn)(void *data,
                                       const grpc_call_stats *stats);

/** Register \a fn as the function to be invoked by the load reporting filter,
 * passing \a data as its namesake argument. To be called only from a plugin
 * init function. */
grpc_load_reporting_data *grpc_load_reporting_create(grpc_load_reporting_fn fn,
                                                     void *data);

// XXX
void grpc_load_reporting_destroy(grpc_load_reporting_data *lrd);

/** Invoke the function registered by \a grpc_load_reporting_init, passing it \a
 * stats as one of the arguments (see \a load_reporting_fn). */
void grpc_load_reporting_call(grpc_load_reporting_data *lrd,
                              const grpc_call_stats *stats);

#endif /* GRPC_CORE_EXT_LOAD_REPORTING_LOAD_REPORTING_H */
