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

#include "src/core/lib/iomgr/endpoint_pair.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

static void clean_up(void) {}

static grpc_endpoint_test_fixture create_fixture_endpoint_pair(
    size_t slice_size) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_endpoint_test_fixture f;
  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = (int)slice_size;
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  grpc_endpoint_pair p = grpc_iomgr_create_endpoint_pair("test", &args);

  f.client_ep = p.client;
  f.server_ep = p.server;
  grpc_endpoint_add_to_pollset(&exec_ctx, f.client_ep, g_pollset);
  grpc_endpoint_add_to_pollset(&exec_ctx, f.server_ep, g_pollset);
  grpc_exec_ctx_finish(&exec_ctx);

  return f;
}

static grpc_endpoint_test_config configs[] = {
    {"tcp/tcp_socketpair", create_fixture_endpoint_pair, clean_up},
};

static void destroy_pollset(grpc_exec_ctx* exec_ctx, void* p,
                            grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(g_pollset, &g_mu);
  grpc_endpoint_tests(configs[0], g_pollset, g_mu);
  GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, g_pollset, &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();
  gpr_free(g_pollset);

  return 0;
}
