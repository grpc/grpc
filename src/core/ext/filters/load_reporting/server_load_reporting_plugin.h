/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_PLUGIN_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_PLUGIN_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_stack.h"

#ifdef __cplusplus
extern "C" {
#endif

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
  const grpc_call_final_info* final_info;

  const char* initial_md_string;  /**< value string for LR's initial md key */
  const char* trailing_md_string; /**< value string for LR's trailing md key */
  const char* method_name;        /**< Corresponds to :path header */
} grpc_load_reporting_call_data;

/** Return a \a grpc_arg enabling load reporting */
grpc_arg grpc_load_reporting_enable_arg();

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_PLUGIN_H \
        */
