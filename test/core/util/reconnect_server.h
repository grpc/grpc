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

#ifndef GRPC_TEST_CORE_UTIL_RECONNECT_SERVER_H
#define GRPC_TEST_CORE_UTIL_RECONNECT_SERVER_H

#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "test/core/util/test_tcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timestamp_list {
  gpr_timespec timestamp;
  struct timestamp_list *next;
} timestamp_list;

typedef struct reconnect_server {
  test_tcp_server tcp_server;
  timestamp_list *head;
  timestamp_list *tail;
  char *peer;
  int max_reconnect_backoff_ms;
} reconnect_server;

void reconnect_server_init(reconnect_server *server);
void reconnect_server_start(reconnect_server *server, int port);
void reconnect_server_poll(reconnect_server *server, int seconds);
void reconnect_server_destroy(reconnect_server *server);
void reconnect_server_clear_timestamps(reconnect_server *server);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_TEST_CORE_UTIL_RECONNECT_SERVER_H */
