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

#include <grpc/support/port_platform.h>

#include <grpc/census.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include "src/core/channel/channel_stack.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/client_config/resolvers/dns_resolver.h"
#include "src/core/client_config/resolvers/sockaddr_resolver.h"
#include "src/core/debug/trace.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/profiling/timers.h"
#include "src/core/surface/call.h"
#include "src/core/surface/init.h"
#include "src/core/surface/surface_trace.h"
#include "src/core/transport/chttp2_transport.h"

static gpr_once g_basic_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;
static int g_initializations;

static void do_basic_init(void) {
  gpr_mu_init(&g_init_mu);
  g_initializations = 0;
}

void grpc_init(void) {
  gpr_once_init(&g_basic_init, do_basic_init);

  gpr_mu_lock(&g_init_mu);
  if (++g_initializations == 1) {
    gpr_time_init();
    grpc_resolver_registry_init("dns:///");
    grpc_register_resolver_type("dns", grpc_dns_resolver_factory_create());
    grpc_register_resolver_type("ipv4", grpc_ipv4_resolver_factory_create());
    grpc_register_resolver_type("ipv6", grpc_ipv6_resolver_factory_create());
#ifdef GPR_POSIX_SOCKET
    grpc_register_resolver_type("unix", grpc_unix_resolver_factory_create());
#endif
    grpc_register_tracer("channel", &grpc_trace_channel);
    grpc_register_tracer("surface", &grpc_surface_trace);
    grpc_register_tracer("http", &grpc_http_trace);
    grpc_register_tracer("flowctl", &grpc_flowctl_trace);
    grpc_register_tracer("batch", &grpc_trace_batch);
    grpc_security_pre_init();
    grpc_iomgr_init();
    grpc_tracer_init("GRPC_TRACE");
    /* Only initialize census if noone else has. */
    if (census_enabled() == CENSUS_FEATURE_NONE) {
      if (census_initialize(census_supported())) { /* enable all features. */
        gpr_log(GPR_ERROR, "Could not initialize census.");
      }
    }
    grpc_timers_global_init();
  }
  gpr_mu_unlock(&g_init_mu);
}

void grpc_shutdown(void) {
  gpr_mu_lock(&g_init_mu);
  if (--g_initializations == 0) {
    grpc_iomgr_shutdown();
    census_shutdown();
    grpc_timers_global_destroy();
    grpc_tracer_shutdown();
    grpc_resolver_registry_shutdown();
  }
  gpr_mu_unlock(&g_init_mu);
}

int grpc_is_initialized(void) {
  int r;
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  r = g_initializations > 0;
  gpr_mu_unlock(&g_init_mu);
  return r;
}
