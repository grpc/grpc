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

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "src/core/lib/channel/handshaker_factory.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/security/transport/security_handshaker.h"

#include "test/core/handshake/server_ssl_common.h"

/* The purpose of this test is to exercise the case when a
 * grpc *security_handshaker* begins its handshake with data already
 * in the read buffer of the handshaker arg. This scenario is created by
 * adding a fake "readahead" handshaker at the beginning of the server's
 * handshaker list, which just reads from the connection and then places
 * read bytes into the read buffer of the handshake arg (to be passed down
 * to the security_handshaker). This test is meant to protect code relying on
 * this functionality that lives outside of this repo. */

static void readahead_handshaker_destroy(grpc_exec_ctx* ctx,
                                         grpc_handshaker* handshaker) {
  gpr_free(handshaker);
}

static void readahead_handshaker_shutdown(grpc_exec_ctx* ctx,
                                          grpc_handshaker* handshaker,
                                          grpc_error* error) {}

static void readahead_handshaker_do_handshake(
    grpc_exec_ctx* ctx, grpc_handshaker* handshaker,
    grpc_tcp_server_acceptor* acceptor, grpc_closure* on_handshake_done,
    grpc_handshaker_args* args) {
  grpc_endpoint_read(ctx, args->endpoint, args->read_buffer, on_handshake_done);
}

const grpc_handshaker_vtable readahead_handshaker_vtable = {
    readahead_handshaker_destroy, readahead_handshaker_shutdown,
    readahead_handshaker_do_handshake};

static grpc_handshaker* readahead_handshaker_create(grpc_exec_ctx* ctx) {
  grpc_handshaker* h = (grpc_handshaker*)gpr_zalloc(sizeof(grpc_handshaker));
  grpc_handshaker_init(&readahead_handshaker_vtable, h);
  return h;
}

static void readahead_handshaker_factory_add_handshakers(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory* hf,
    const grpc_channel_args* args, grpc_handshake_manager* handshake_mgr) {
  grpc_handshake_manager_add(handshake_mgr,
                             readahead_handshaker_create(exec_ctx));
}

static void readahead_handshaker_factory_destroy(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory* handshaker_factory) {}

static const grpc_handshaker_factory_vtable
    readahead_handshaker_factory_vtable = {
        readahead_handshaker_factory_add_handshakers,
        readahead_handshaker_factory_destroy};

int main(int argc, char* argv[]) {
  grpc_handshaker_factory readahead_handshaker_factory = {
      &readahead_handshaker_factory_vtable};
  grpc_init();
  grpc_handshaker_factory_register(true /* at_start */, HANDSHAKER_SERVER,
                                   &readahead_handshaker_factory);
  const char* full_alpn_list[] = {"grpc-exp", "h2"};
  GPR_ASSERT(server_ssl_test(full_alpn_list, 2, "grpc-exp"));
  grpc_shutdown();
  return 0;
}
