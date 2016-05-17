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

#ifndef GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H
#define GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H

#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/tcp_server.h"

typedef struct test_tcp_server {
  grpc_tcp_server *tcp_server;
  grpc_closure shutdown_complete;
  int shutdown;
  gpr_mu *mu;
  grpc_pollset *pollset;
  grpc_tcp_server_cb on_connect;
  void *cb_data;
} test_tcp_server;

void test_tcp_server_init(test_tcp_server *server,
                          grpc_tcp_server_cb on_connect, void *user_data);
void test_tcp_server_start(test_tcp_server *server, int port);
void test_tcp_server_poll(test_tcp_server *server, int seconds);
void test_tcp_server_destroy(test_tcp_server *server);

#endif /* GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H */
