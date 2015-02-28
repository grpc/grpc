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
#ifdef GPR_POSIX_SOCKET

#include "test/core/util/port.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/log.h>

#define NUM_RANDOM_PORTS_TO_PICK 100

static int is_port_available(int *port, int is_tcp) {
  const int proto = is_tcp ? IPPROTO_TCP : 0;
  const int fd = socket(AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM, proto);
  int one = 1;
  struct sockaddr_in addr;
  socklen_t alen = sizeof(addr);
  int actual_port;

  GPR_ASSERT(*port >= 0);
  GPR_ASSERT(*port <= 65535);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "socket() failed: %s", strerror(errno));
    return 0;
  }

  /* Reuseaddr lets us start up a server immediately after it exits */
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    gpr_log(GPR_ERROR, "setsockopt() failed: %s", strerror(errno));
    close(fd);
    return 0;
  }

  /* Try binding to port */
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(*port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    gpr_log(GPR_DEBUG, "bind(port=%d) failed: %s", *port, strerror(errno));
    close(fd);
    return 0;
  }

  /* Get the bound port number */
  if (getsockname(fd, (struct sockaddr *)&addr, &alen) < 0) {
    gpr_log(GPR_ERROR, "getsockname() failed: %s", strerror(errno));
    close(fd);
    return 0;
  }
  GPR_ASSERT(alen <= sizeof(addr));
  actual_port = ntohs(addr.sin_port);
  GPR_ASSERT(actual_port > 0);
  if (*port == 0) {
    *port = actual_port;
  } else {
    GPR_ASSERT(*port == actual_port);
  }

  close(fd);
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

  /* In alternating iterations we try UDP ports before TCP ports UDP
     ports -- it could be the case that this machine has been using up
     UDP ports and they are scarcer. */

  /* Type of port to first pick in next iteration */
  int is_tcp = 1;
  int try = 0;

  for (;;) {
    int port;
    try++;
    if (try == 1) {
      port = getpid() % (65536 - 30000) + 30000;
    } else if (try <= NUM_RANDOM_PORTS_TO_PICK) {
      port = rand() % (65536 - 30000) + 30000;
    } else {
      port = 0;
    }
    
    if (!is_port_available(&port, is_tcp)) {
      continue;
    }

    GPR_ASSERT(port > 0);
    /* Check that the port # is free for the other type of socket also */
    if (!is_port_available(&port, !is_tcp)) {
      /* In the next iteration try to bind to the other type first
         because perhaps it is more rare. */
      is_tcp = !is_tcp;
      continue;
    }

    /* TODO(ctiller): consider caching this port in some structure, to avoid
                      handing it out again */

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

#endif /* GPR_POSIX_SOCKET */
