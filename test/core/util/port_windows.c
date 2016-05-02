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
#include "test/core/util/test_config.h"
#if defined(GPR_WINSOCK_SOCKET) && defined(GRPC_TEST_PICK_PORT)

#include "test/core/util/port.h"

#include <errno.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/env.h"
#include "test/core/util/port_server_client.h"

#if GPR_GETPID_IN_UNISTD_H
#include <sys/unistd.h>
static int _getpid() { return getpid(); }
#endif

#define NUM_RANDOM_PORTS_TO_PICK 100

static int *chosen_ports = NULL;
static size_t num_chosen_ports = 0;

static int has_port_been_chosen(int port) {
  size_t i;
  for (i = 0; i < num_chosen_ports; i++) {
    if (chosen_ports[i] == port) {
      return 1;
    }
  }
  return 0;
}

static void free_chosen_ports(void) {
  char *env = gpr_getenv("GRPC_TEST_PORT_SERVER");
  if (env != NULL) {
    size_t i;
    for (i = 0; i < num_chosen_ports; i++) {
      grpc_free_port_using_server(env, chosen_ports[i]);
    }
    gpr_free(env);
  }

  gpr_free(chosen_ports);
}

static void chose_port(int port) {
  if (chosen_ports == NULL) {
    atexit(free_chosen_ports);
  }
  num_chosen_ports++;
  chosen_ports = gpr_realloc(chosen_ports, sizeof(int) * num_chosen_ports);
  chosen_ports[num_chosen_ports - 1] = port;
}

static int is_port_available(int *port, int is_tcp) {
  const int proto = is_tcp ? IPPROTO_TCP : 0;
  const SOCKET fd = socket(AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM, proto);
  int one = 1;
  struct sockaddr_in addr;
  socklen_t alen = sizeof(addr);
  int actual_port;

  GPR_ASSERT(*port >= 0);
  GPR_ASSERT(*port <= 65535);
  if (INVALID_SOCKET == fd) {
    gpr_log(GPR_ERROR, "socket() failed: %s", strerror(errno));
    return 0;
  }

  /* Reuseaddr lets us start up a server immediately after it exits */
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
                 sizeof(one)) < 0) {
    gpr_log(GPR_ERROR, "setsockopt() failed: %s", strerror(errno));
    closesocket(fd);
    return 0;
  }

  /* Try binding to port */
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((u_short)*port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    gpr_log(GPR_DEBUG, "bind(port=%d) failed: %s", *port, strerror(errno));
    closesocket(fd);
    return 0;
  }

  /* Get the bound port number */
  if (getsockname(fd, (struct sockaddr *)&addr, &alen) < 0) {
    gpr_log(GPR_ERROR, "getsockname() failed: %s", strerror(errno));
    closesocket(fd);
    return 0;
  }
  GPR_ASSERT(alen <= (socklen_t)sizeof(addr));
  actual_port = ntohs(addr.sin_port);
  GPR_ASSERT(actual_port > 0);
  if (*port == 0) {
    *port = actual_port;
  } else {
    GPR_ASSERT(*port == actual_port);
  }

  closesocket(fd);
  return 1;
}

int grpc_pick_unused_port(void) {
  /* We repeatedly pick a port and then see whether or not it is
     available for use both as a TCP socket and a UDP socket.  First, we
     pick a random large port number.  For subsequent
     iterations, we bind to an anonymous port and let the OS pick the
     port number.  The random port picking reduces the probability of
     races with other processes on kernels that want to reuse the same
     port numbers over and over. */

  /* In alternating iterations we trial UDP ports before TCP ports UDP
     ports -- it could be the case that this machine has been using up
     UDP ports and they are scarcer. */

  /* Type of port to first pick in next iteration */
  int is_tcp = 1;
  int trial = 0;

  char *env = gpr_getenv("GRPC_TEST_PORT_SERVER");
  if (env) {
    int port = grpc_pick_port_using_server(env);
    gpr_free(env);
    if (port != 0) {
      return port;
    }
  }

  for (;;) {
    int port;
    trial++;
    if (trial == 1) {
      port = _getpid() % (65536 - 30000) + 30000;
    } else if (trial <= NUM_RANDOM_PORTS_TO_PICK) {
      port = rand() % (65536 - 30000) + 30000;
    } else {
      port = 0;
    }

    if (has_port_been_chosen(port)) {
      continue;
    }

    if (!is_port_available(&port, is_tcp)) {
      continue;
    }

    GPR_ASSERT(port > 0);
    /* Check that the port # is free for the other type of socket also */
    if (!is_port_available(&port, !is_tcp)) {
      /* In the next iteration trial to bind to the other type first
         because perhaps it is more rare. */
      is_tcp = !is_tcp;
      continue;
    }

    /* TODO(ctiller): consider caching this port in some structure, to avoid
       handing it out again */

    chose_port(port);
    return port;
  }

  /* The port iterator reached the end without finding a suitable port. */
  return 0;
}

int grpc_pick_unused_port_or_die(void) {
  int port = grpc_pick_unused_port();
  GPR_ASSERT(port > 0);
  return port;
}

#endif /* GPR_WINSOCK_SOCKET && GRPC_TEST_PICK_PORT */
