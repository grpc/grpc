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
#if defined(GPR_POSIX_SOCKET) && defined(GRPC_TEST_PICK_PORT)

#include "test/core/util/port.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/httpcli/httpcli.h"
#include "src/core/support/env.h"

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

typedef struct freereq {
  grpc_pollset pollset;
  int done;
} freereq;

static void destroy_pollset_and_shutdown(grpc_exec_ctx *exec_ctx, void *p,
                                         int success) {
  grpc_pollset_destroy(p);
  grpc_shutdown();
}

static void freed_port_from_server(grpc_exec_ctx *exec_ctx, void *arg,
                                   const grpc_httpcli_response *response) {
  freereq *pr = arg;
  gpr_mu_lock(GRPC_POLLSET_MU(&pr->pollset));
  pr->done = 1;
  grpc_pollset_kick(&pr->pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(&pr->pollset));
}

static void free_port_using_server(char *server, int port) {
  grpc_httpcli_context context;
  grpc_httpcli_request req;
  freereq pr;
  char *path;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure shutdown_closure;

  grpc_init();

  memset(&pr, 0, sizeof(pr));
  memset(&req, 0, sizeof(req));
  grpc_pollset_init(&pr.pollset);
  grpc_closure_init(&shutdown_closure, destroy_pollset_and_shutdown,
                    &pr.pollset);

  req.host = server;
  gpr_asprintf(&path, "/drop/%d", port);
  req.path = path;

  grpc_httpcli_context_init(&context);
  grpc_httpcli_get(&exec_ctx, &context, &pr.pollset, &req,
                   GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10), freed_port_from_server,
                   &pr);
  gpr_mu_lock(GRPC_POLLSET_MU(&pr.pollset));
  while (!pr.done) {
    grpc_pollset_worker worker;
    grpc_pollset_work(&exec_ctx, &pr.pollset, &worker,
                      gpr_now(GPR_CLOCK_MONOTONIC),
                      GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1));
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&pr.pollset));

  grpc_httpcli_context_destroy(&context);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, &pr.pollset, &shutdown_closure);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(path);
}

static void free_chosen_ports() {
  char *env = gpr_getenv("GRPC_TEST_PORT_SERVER");
  if (env != NULL) {
    size_t i;
    for (i = 0; i < num_chosen_ports; i++) {
      free_port_using_server(env, chosen_ports[i]);
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
  addr.sin_port = htons((gpr_uint16)*port);
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

typedef struct portreq {
  grpc_pollset pollset;
  int port;
  int retries;
  char *server;
  grpc_httpcli_context *ctx;
} portreq;

static void got_port_from_server(grpc_exec_ctx *exec_ctx, void *arg,
                                 const grpc_httpcli_response *response) {
  size_t i;
  int port = 0;
  portreq *pr = arg;
  int failed = 0;

  if (!response) {
    failed = 1;
    gpr_log(GPR_DEBUG,
            "failed port pick from server: retrying [response=NULL]");
  } else if (response->status != 200) {
    failed = 1;
    gpr_log(GPR_DEBUG, "failed port pick from server: status=%d",
            response->status);
  }

  if (failed) {
    grpc_httpcli_request req;
    memset(&req, 0, sizeof(req));
    GPR_ASSERT(pr->retries < 10);
    pr->retries++;
    req.host = pr->server;
    req.path = "/get";
    sleep(1);
    grpc_httpcli_get(exec_ctx, pr->ctx, &pr->pollset, &req,
                     GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10), got_port_from_server,
                     pr);
    return;
  }
  GPR_ASSERT(response);
  GPR_ASSERT(response->status == 200);
  for (i = 0; i < response->body_length; i++) {
    GPR_ASSERT(response->body[i] >= '0' && response->body[i] <= '9');
    port = port * 10 + response->body[i] - '0';
  }
  GPR_ASSERT(port > 1024);
  gpr_mu_lock(GRPC_POLLSET_MU(&pr->pollset));
  pr->port = port;
  grpc_pollset_kick(&pr->pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(&pr->pollset));
}

static int pick_port_using_server(char *server) {
  grpc_httpcli_context context;
  grpc_httpcli_request req;
  portreq pr;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure shutdown_closure;

  grpc_init();

  memset(&pr, 0, sizeof(pr));
  memset(&req, 0, sizeof(req));
  grpc_pollset_init(&pr.pollset);
  grpc_closure_init(&shutdown_closure, destroy_pollset_and_shutdown,
                    &pr.pollset);
  pr.port = -1;
  pr.server = server;
  pr.ctx = &context;

  req.host = server;
  req.path = "/get";

  grpc_httpcli_context_init(&context);
  grpc_httpcli_get(&exec_ctx, &context, &pr.pollset, &req,
                   GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10), got_port_from_server,
                   &pr);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_mu_lock(GRPC_POLLSET_MU(&pr.pollset));
  while (pr.port == -1) {
    grpc_pollset_worker worker;
    grpc_pollset_work(&exec_ctx, &pr.pollset, &worker,
                      gpr_now(GPR_CLOCK_MONOTONIC),
                      GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1));
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&pr.pollset));

  grpc_httpcli_context_destroy(&context);
  grpc_pollset_shutdown(&exec_ctx, &pr.pollset, &shutdown_closure);
  grpc_exec_ctx_finish(&exec_ctx);

  return pr.port;
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
    int port = pick_port_using_server(env);
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

#endif /* GPR_POSIX_SOCKET && GRPC_TEST_PICK_PORT */
