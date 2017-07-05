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

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <string.h>

#include <grpc/census.h>

#include "src/core/ext/census/grpc_filter.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"

static bool is_census_enabled(const grpc_channel_args *a) {
  size_t i;
  if (a == NULL) return 0;
  for (i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, GRPC_ARG_ENABLE_CENSUS)) {
      return a->args[i].value.integer != 0 && census_enabled();
    }
  }
  return census_enabled() && !grpc_channel_args_want_minimal_stack(a);
}

static bool maybe_add_census_filter(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_stack_builder *builder,
                                    void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (is_census_enabled(args)) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, (const grpc_channel_filter *)arg, NULL, NULL);
  }
  return true;
}

void census_grpc_plugin_init(void) {
  /* Only initialize census if no one else has and some features are
   * available. */
  if (census_enabled() == CENSUS_FEATURE_NONE &&
      census_supported() != CENSUS_FEATURE_NONE) {
    if (census_initialize(census_supported())) { /* enable all features. */
      gpr_log(GPR_ERROR, "Could not initialize census.");
    }
  }
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX,
                                   maybe_add_census_filter,
                                   (void *)&grpc_client_census_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_census_filter,
                                   (void *)&grpc_server_census_filter);
}

void census_grpc_plugin_shutdown(void) { census_shutdown(); }
