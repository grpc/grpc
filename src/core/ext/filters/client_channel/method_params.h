/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace internal {

typedef enum {
  WAIT_FOR_READY_UNSET = 0,  // zero so it can be default initialized
  WAIT_FOR_READY_FALSE,
  WAIT_FOR_READY_TRUE
} wait_for_ready_value;

typedef struct {
  int max_attempts;
  grpc_millis initial_backoff;
  grpc_millis max_backoff;
  float backoff_multiplier;
  grpc_status_code* retryable_status_codes;
  size_t num_retryable_status_codes;
} retry_policy_params;

typedef struct {
  gpr_refcount refs;
  grpc_millis timeout;
  wait_for_ready_value wait_for_ready;
  retry_policy_params* retry_policy;
} method_parameters;

method_parameters* method_parameters_ref(
    method_parameters* method_params);

void method_parameters_unref(method_parameters* method_params);

void* method_parameters_create_from_json(const grpc_json* json,
                                         void* user_data);

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H */
