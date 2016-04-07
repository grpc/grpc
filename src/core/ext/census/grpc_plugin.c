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
  return census_enabled();
}

static bool maybe_add_census_filter(grpc_channel_stack_builder *builder,
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
