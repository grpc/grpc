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

#include "test/core/util/reconnect_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/iomgr/endpoint.h"
#include "src/core/iomgr/sockaddr.h"
#include "src/core/iomgr/tcp_server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_tcp_server.h"

static void pretty_print_backoffs(reconnect_server *server) {
  gpr_timespec diff;
  int i = 1;
  double expected_backoff = 1000.0, backoff;
  timestamp_list *head = server->head;
  gpr_log(GPR_INFO, "reconnect server: new connection");
  for (head = server->head; head && head->next; head = head->next, i++) {
    diff = gpr_time_sub(head->next->timestamp, head->timestamp);
    backoff = gpr_time_to_millis(diff);
    gpr_log(GPR_INFO,
            "retry %2d:backoff %6.2fs,expected backoff %6.2fs, jitter %4.2f%%",
            i, backoff / 1000.0, expected_backoff / 1000.0,
            (backoff - expected_backoff) * 100.0 / expected_backoff);
    expected_backoff *= 1.6;
    if (expected_backoff > 120 * 1000) {
      expected_backoff = 120 * 1000;
    }
  }
}

static void on_connect(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *tcp) {
  char *peer;
  char *last_colon;
  reconnect_server *server = (reconnect_server *)arg;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  timestamp_list *new_tail;
  peer = grpc_endpoint_get_peer(tcp);
  grpc_endpoint_shutdown(exec_ctx, tcp);
  grpc_endpoint_destroy(exec_ctx, tcp);
  if (peer) {
    last_colon = strrchr(peer, ':');
    if (server->peer == NULL) {
      server->peer = peer;
    } else {
      if (last_colon == NULL) {
        gpr_log(GPR_ERROR, "peer does not contain a ':'");
      } else if (strncmp(server->peer, peer, (size_t)(last_colon - peer)) !=
                 0) {
        gpr_log(GPR_ERROR, "mismatched peer! %s vs %s", server->peer, peer);
      }
      gpr_free(peer);
    }
  }
  new_tail = gpr_malloc(sizeof(timestamp_list));
  new_tail->timestamp = now;
  new_tail->next = NULL;
  if (server->tail == NULL) {
    server->head = new_tail;
    server->tail = new_tail;
  } else {
    server->tail->next = new_tail;
    server->tail = new_tail;
  }
  pretty_print_backoffs(server);
}

void reconnect_server_init(reconnect_server *server) {
  test_tcp_server_init(&server->tcp_server, on_connect, server);
  server->head = NULL;
  server->tail = NULL;
  server->peer = NULL;
}

void reconnect_server_start(reconnect_server *server, int port) {
  test_tcp_server_start(&server->tcp_server, port);
}

void reconnect_server_poll(reconnect_server *server, int seconds) {
  test_tcp_server_poll(&server->tcp_server, seconds);
}

void reconnect_server_clear_timestamps(reconnect_server *server) {
  timestamp_list *new_head = server->head;
  while (server->head) {
    new_head = server->head->next;
    gpr_free(server->head);
    server->head = new_head;
  }
  server->tail = NULL;
  gpr_free(server->peer);
  server->peer = NULL;
}

void reconnect_server_destroy(reconnect_server *server) {
  reconnect_server_clear_timestamps(server);
  test_tcp_server_destroy(&server->tcp_server);
}
