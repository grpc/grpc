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

#include "test/core/util/reconnect_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/tcp_server.h"
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
    int max_reconnect_backoff_ms = 120 * 1000;
    if (server->max_reconnect_backoff_ms > 0) {
      max_reconnect_backoff_ms = server->max_reconnect_backoff_ms;
    }
    if (expected_backoff > max_reconnect_backoff_ms) {
      expected_backoff = max_reconnect_backoff_ms;
    }
  }
}

static void on_connect(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *tcp,
                       grpc_pollset *accepting_pollset,
                       grpc_tcp_server_acceptor *acceptor) {
  gpr_free(acceptor);
  char *peer;
  char *last_colon;
  reconnect_server *server = (reconnect_server *)arg;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  timestamp_list *new_tail;
  peer = grpc_endpoint_get_peer(tcp);
  grpc_endpoint_shutdown(exec_ctx, tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connected"));
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
  server->max_reconnect_backoff_ms = 0;
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
