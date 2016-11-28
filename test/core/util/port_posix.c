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

#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"
#if defined(GRPC_POSIX_SOCKET) && defined(GRPC_TEST_PICK_PORT)

#include "test/core/util/port.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/env.h"
#include "test/core/util/port_server_client.h"

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

static int free_chosen_port(int port) {
  size_t i;
  int found = 0;
  size_t found_at = 0;
  char *env = gpr_getenv("GRPC_TEST_PORT_SERVER");
  /* Find the port and erase it from the list, then tell the server it can be
     freed. */
  for (i = 0; i < num_chosen_ports; i++) {
    if (chosen_ports[i] == port) {
      GPR_ASSERT(found == 0);
      found = 1;
      found_at = i;
    }
  }
  if (found) {
    chosen_ports[found_at] = chosen_ports[num_chosen_ports - 1];
    num_chosen_ports--;
    if (env) {
      grpc_free_port_using_server(env, port);
    }
  }
  gpr_free(env);
  return found;
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

static bool is_port_available(int *port, bool is_tcp) {
  GPR_ASSERT(*port >= 0);
  GPR_ASSERT(*port <= 65535);

  /* For a port to be considered available, the kernel must support
     at least one of (IPv6, IPv4), and the port must be available
     on each supported family. */
  bool got_socket = false;
  for (int is_ipv6 = 1; is_ipv6 >= 0; is_ipv6--) {
    const int fd =
        socket(is_ipv6 ? AF_INET6 : AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM,
               is_tcp ? IPPROTO_TCP : 0);
    if (fd >= 0) {
      got_socket = true;
    } else {
      continue;
    }

    /* Reuseaddr lets us start up a server immediately after it exits */
    const int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
      gpr_log(GPR_ERROR, "setsockopt() failed: %s", strerror(errno));
      close(fd);
      return false;
    }

    /* Try binding to port */
    grpc_resolved_address addr;
    if (is_ipv6) {
      grpc_sockaddr_make_wildcard6(*port, &addr); /* [::]:port */
    } else {
      grpc_sockaddr_make_wildcard4(*port, &addr); /* 0.0.0.0:port */
    }
    if (bind(fd, (struct sockaddr *)addr.addr, (socklen_t)addr.len) < 0) {
      gpr_log(GPR_DEBUG, "bind(port=%d) failed: %s", *port, strerror(errno));
      close(fd);
      return false;
    }

    /* Get the bound port number */
    if (getsockname(fd, (struct sockaddr *)addr.addr, (socklen_t *)&addr.len) <
        0) {
      gpr_log(GPR_ERROR, "getsockname() failed: %s", strerror(errno));
      close(fd);
      return false;
    }
    GPR_ASSERT(addr.len <= sizeof(addr.addr));
    const int actual_port = grpc_sockaddr_get_port(&addr);
    GPR_ASSERT(actual_port > 0);
    if (*port == 0) {
      *port = actual_port;
    } else {
      GPR_ASSERT(*port == actual_port);
    }

    close(fd);
  }
  if (!got_socket) {
    gpr_log(GPR_ERROR, "socket() failed: %s", strerror(errno));
    return false;
  }
  return true;
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
  bool is_tcp = true;
  int trial = 0;

  char *env = gpr_getenv("GRPC_TEST_PORT_SERVER");
  if (env) {
    int port = grpc_pick_port_using_server(env);
    gpr_free(env);
    if (port != 0) {
      chose_port(port);
    }
    return port;
  }

  for (;;) {
    int port;
    trial++;
    if (trial == 1) {
      port = getpid() % (65536 - 30000) + 30000;
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

void grpc_recycle_unused_port(int port) { GPR_ASSERT(free_chosen_port(port)); }

#endif /* GRPC_POSIX_SOCKET && GRPC_TEST_PICK_PORT */
