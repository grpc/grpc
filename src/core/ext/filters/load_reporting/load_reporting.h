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

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_LOAD_REPORTING_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_LOAD_REPORTING_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_stack.h"

/** Identifiers for the invocation point of the users LR callback */
typedef enum grpc_load_reporting_source {
  GRPC_LR_POINT_UNKNOWN = 0,
  GRPC_LR_POINT_CHANNEL_CREATION,
  GRPC_LR_POINT_CHANNEL_DESTRUCTION,
  GRPC_LR_POINT_CALL_CREATION,
  GRPC_LR_POINT_CALL_DESTRUCTION
} grpc_load_reporting_source;

/** Call information to be passed to the provided LR callback. */
typedef struct grpc_load_reporting_call_data {
  const grpc_load_reporting_source source; /**< point of last data update. */

  /** Unique identifier for the channel associated with the data */
  intptr_t channel_id;

  /** Unique identifier for the call associated with the data. If the call
   * hasn't been created yet, it'll have a value of zero. */
  intptr_t call_id;

  /** Only valid when \a source is \a GRPC_LR_POINT_CALL_DESTRUCTION, that is,
   * once the call has completed */
  const grpc_call_final_info *final_info;

  const char *initial_md_string;  /**< value string for LR's initial md key */
  const char *trailing_md_string; /**< value string for LR's trailing md key */
  const char *method_name;        /**< Corresponds to :path header */
} grpc_load_reporting_call_data;

/** Return a \a grpc_arg enabling load reporting */
grpc_arg grpc_load_reporting_enable_arg();

#endif /* GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_LOAD_REPORTING_H */
