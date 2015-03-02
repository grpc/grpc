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

#include <grpc/grpc.h>
#include "src/core/iomgr/iomgr.h"
#include "src/core/debug/trace.h"
#include "src/core/statistics/census_interface.h"
#include "src/core/channel/channel_stack.h"
#include "src/core/surface/init.h"
#include "src/core/surface/surface_trace.h"
#include "src/core/transport/chttp2_transport.h"

static gpr_once g_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;
static int g_initializations;

static void do_init(void) {
  gpr_mu_init(&g_init_mu);
  g_initializations = 0;
}

void grpc_init(void) {
  gpr_once_init(&g_init, do_init);

  gpr_mu_lock(&g_init_mu);
  if (++g_initializations == 1) {
    grpc_register_tracer("channel", &grpc_trace_channel);
    grpc_register_tracer("surface", &grpc_surface_trace);
    grpc_register_tracer("http", &grpc_http_trace);
    grpc_security_pre_init();
    grpc_tracer_init("GRPC_TRACE");
    grpc_iomgr_init();
    census_init();
  }
  gpr_mu_unlock(&g_init_mu);
}

void grpc_shutdown(void) {
  gpr_mu_lock(&g_init_mu);
  if (--g_initializations == 0) {
    grpc_iomgr_shutdown();
    census_shutdown();
  }
  gpr_mu_unlock(&g_init_mu);
}
