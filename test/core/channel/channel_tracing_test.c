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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_tracer.h"

#include "test/core/util/test_config.h"

static void test_channel_tracing(void) {
  grpc_channel_tracer* tracer = grpc_channel_tracer_init_tracer();
  grpc_subchannel_tracer* sc1 = grpc_subchannel_tracer_init_tracer();
  grpc_subchannel_tracer* sc2 = grpc_subchannel_tracer_init_tracer();
  char* test = strdup("test1");
  char* test2 = strdup("test2");
  char* test3 = strdup("test3");
  grpc_channel_tracer_add_subchannel(tracer, sc1);
  grpc_channel_tracer_add_subchannel(tracer, sc2);
  grpc_channel_tracer_add_trace(
      &tracer->node_list, test, GRPC_ERROR_CREATE("Created Error"),
      gpr_now(GPR_CLOCK_REALTIME), GRPC_CHANNEL_READY);
  grpc_channel_tracer_add_trace(&tracer->node_list, test2, GRPC_ERROR_NONE,
                                gpr_now(GPR_CLOCK_REALTIME),
                                GRPC_CHANNEL_READY);
  grpc_channel_tracer_add_trace(&tracer->node_list, test3, GRPC_ERROR_CANCELLED,
                                gpr_now(GPR_CLOCK_REALTIME),
                                GRPC_CHANNEL_READY);
  char* sct1 = strdup("sc1");
  char* sct11 = strdup("sc11");
  char* sct2 = strdup("sc2");
  grpc_channel_tracer_add_trace(&sc1->node_list, sct1, GRPC_ERROR_NONE,
                                gpr_now(GPR_CLOCK_REALTIME),
                                GRPC_CHANNEL_READY);
  grpc_channel_tracer_add_trace(&sc1->node_list, sct11,
                                GRPC_ERROR_CREATE("Some Other Error"),
                                gpr_now(GPR_CLOCK_REALTIME), GRPC_CHANNEL_IDLE);
  grpc_channel_tracer_add_trace(&sc2->node_list, sct2, GRPC_ERROR_NONE,
                                gpr_now(GPR_CLOCK_REALTIME),
                                GRPC_CHANNEL_READY);
  grpc_channel_tracer_log_trace(tracer);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_channel_tracing();
  grpc_shutdown();
  return 0;
}
