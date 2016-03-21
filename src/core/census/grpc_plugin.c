/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/census/grpc_plugin.h"

#include <limits.h>

#include <grpc/census.h>

#include "src/core/census/grpc_filter.h"
#include "src/core/surface/channel_init.h"
#include "src/core/channel/channel_stack_builder.h"

static bool maybe_add_census_filter(grpc_channel_stack_builder *builder,
                                    void *arg_must_be_null) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (grpc_channel_args_is_census_enabled(args)) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, &grpc_client_census_filter, NULL, NULL);
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
                                   maybe_add_census_filter, NULL);
  grpc_channel_init_register_stage(GRPC_CLIENT_UCHANNEL, INT_MAX,
                                   maybe_add_census_filter, NULL);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_census_filter, NULL);
}

void census_grpc_plugin_destroy(void) { census_shutdown(); }
