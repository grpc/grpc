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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/ext/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "test/core/util/test_config.h"

static gpr_mu g_mu;
static bool g_fail_resolution = true;

static grpc_error *my_resolve_address(const char *name, const char *addr,
                                      grpc_resolved_addresses **addrs) {
  gpr_mu_lock(&g_mu);
  GPR_ASSERT(0 == strcmp("test", name));
  if (g_fail_resolution) {
    g_fail_resolution = false;
    gpr_mu_unlock(&g_mu);
    return GRPC_ERROR_CREATE("Forced Failure");
  } else {
    gpr_mu_unlock(&g_mu);
    *addrs = gpr_malloc(sizeof(**addrs));
    (*addrs)->naddrs = 1;
    (*addrs)->addrs = gpr_malloc(sizeof(*(*addrs)->addrs));
    (*addrs)->addrs[0].len = 123;
    return GRPC_ERROR_NONE;
  }
}

static grpc_resolver *create_resolver(grpc_exec_ctx *exec_ctx,
                                      const char *name) {
  grpc_resolver_factory *factory = grpc_resolver_factory_lookup("dns");
  grpc_uri *uri = grpc_uri_parse(name, 0);
  GPR_ASSERT(uri);
  grpc_resolver_args args;
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  grpc_resolver *resolver =
      grpc_resolver_factory_create_resolver(exec_ctx, factory, &args);
  grpc_resolver_factory_unref(factory);
  grpc_uri_destroy(uri);
  return resolver;
}

static void on_done(grpc_exec_ctx *exec_ctx, void *ev, grpc_error *error) {
  gpr_event_set(ev, (void *)1);
}

// interleave waiting for an event with a timer check
static bool wait_loop(int deadline_seconds, gpr_event *ev) {
  while (deadline_seconds) {
    gpr_log(GPR_DEBUG, "Test: waiting for %d more seconds", deadline_seconds);
    if (gpr_event_wait(ev, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1))) return true;
    deadline_seconds--;

    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_timer_check(&exec_ctx, gpr_now(GPR_CLOCK_MONOTONIC), NULL);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  return false;
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  grpc_init();
  gpr_mu_init(&g_mu);
  grpc_blocking_resolve_address = my_resolve_address;
  grpc_channel_args *result = (grpc_channel_args *)1;

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resolver *resolver = create_resolver(&exec_ctx, "dns:test");
  gpr_event ev1;
  gpr_event_init(&ev1);
  grpc_resolver_next(
      &exec_ctx, resolver, &result,
      grpc_closure_create(on_done, &ev1, grpc_schedule_on_exec_ctx));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(wait_loop(5, &ev1));
  GPR_ASSERT(result == NULL);

  gpr_event ev2;
  gpr_event_init(&ev2);
  grpc_resolver_next(
      &exec_ctx, resolver, &result,
      grpc_closure_create(on_done, &ev2, grpc_schedule_on_exec_ctx));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(wait_loop(30, &ev2));
  GPR_ASSERT(result != NULL);

  grpc_channel_args_destroy(&exec_ctx, result);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test");
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);
}
