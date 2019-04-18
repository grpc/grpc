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

#ifndef GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H
#define GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H

#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/tcp_server.h"

typedef struct test_tcp_server {
  grpc_tcp_server* tcp_server;
  grpc_closure shutdown_complete;
  int shutdown;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_tcp_server_cb on_connect;
  void* cb_data;
} test_tcp_server;

void test_tcp_server_init(test_tcp_server* server,
                          grpc_tcp_server_cb on_connect, void* user_data);
void test_tcp_server_start(test_tcp_server* server, int port);
void test_tcp_server_poll(test_tcp_server* server, int seconds);
void test_tcp_server_destroy(test_tcp_server* server);

#endif /* GRPC_TEST_CORE_UTIL_TEST_TCP_SERVER_H */
