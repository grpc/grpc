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

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_posix.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "test/core/util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

/* buffer size used to send and receive data.
   1024 is the minimal value to set TCP send and receive buffer. */
#define BUF_SIZE 1024

/* Create a test socket with the right properties for testing.
   port is the TCP port to listen or connect to.
   Return a socket FD and sockaddr_in. */
static void create_test_socket(int port, int* socket_fd,
                               struct sockaddr_in* sin) {
  int fd;
  int one = 1;
  int buffer_size_bytes = BUF_SIZE;
  int flags;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  /* Reset the size of socket send buffer to the minimal value to facilitate
     buffer filling up and triggering notify_on_write  */
  GPR_ASSERT(grpc_set_socket_sndbuf(fd, buffer_size_bytes) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_set_socket_rcvbuf(fd, buffer_size_bytes) == GRPC_ERROR_NONE);
  /* Make fd non-blocking */
  flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  *socket_fd = fd;

  /* Use local address for test */
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = htonl(0x7f000001);
  GPR_ASSERT(port >= 0 && port < 65536);
  sin->sin_port = htons((uint16_t)port);
}

/* Dummy gRPC callback */
void no_op_cb(void* arg, int success) {}

/* =======An upload server to test notify_on_read===========
   The server simply reads and counts a stream of bytes. */

/* An upload server. */
typedef struct {
  grpc_fd* em_fd;           /* listening fd */
  ssize_t read_bytes_total; /* total number of received bytes */
  int done;                 /* set to 1 when a server finishes serving */
  grpc_closure listen_closure;
} server;

static void server_init(server* sv) {
  sv->read_bytes_total = 0;
  sv->done = 0;
}

/* An upload session.
   Created when a new upload request arrives in the server. */
typedef struct {
  server* sv;              /* not owned by a single session */
  grpc_fd* em_fd;          /* fd to read upload bytes */
  char read_buf[BUF_SIZE]; /* buffer to store upload bytes */
  grpc_closure session_read_closure;
} session;

/* Called when an upload session can be safely shutdown.
   Close session FD and start to shutdown listen FD. */
static void session_shutdown_cb(grpc_exec_ctx* exec_ctx, void* arg, /*session */
                                bool success) {
  session* se = static_cast<session*>(arg);
  server* sv = se->sv;
  grpc_fd_orphan(exec_ctx, se->em_fd, nullptr, nullptr,
                 false /* already_closed */, "a");
  gpr_free(se);
  /* Start to shutdown listen fd. */
  grpc_fd_shutdown(exec_ctx, sv->em_fd,
                   GRPC_ERROR_CREATE_FROM_STATIC_STRING("session_shutdown_cb"));
}

/* Called when data become readable in a session. */
static void session_read_cb(grpc_exec_ctx* exec_ctx, void* arg, /*session */
                            grpc_error* error) {
  session* se = static_cast<session*>(arg);
  int fd = grpc_fd_wrapped_fd(se->em_fd);

  ssize_t read_once = 0;
  ssize_t read_total = 0;

  if (error != GRPC_ERROR_NONE) {
    session_shutdown_cb(exec_ctx, arg, 1);
    return;
  }

  do {
    read_once = read(fd, se->read_buf, BUF_SIZE);
    if (read_once > 0) read_total += read_once;
  } while (read_once > 0);
  se->sv->read_bytes_total += read_total;

  /* read() returns 0 to indicate the TCP connection was closed by the client.
     read(fd, read_buf, 0) also returns 0 which should never be called as such.
     It is possible to read nothing due to spurious edge event or data has
     been drained, In such a case, read() returns -1 and set errno to EAGAIN. */
  if (read_once == 0) {
    session_shutdown_cb(exec_ctx, arg, 1);
  } else if (read_once == -1) {
    if (errno == EAGAIN) {
      /* An edge triggered event is cached in the kernel until next poll.
         In the current single thread implementation, session_read_cb is called
         in the polling thread, such that polling only happens after this
         callback, and will catch read edge event if data is available again
         before notify_on_read.
         TODO(chenw): in multi-threaded version, callback and polling can be
         run in different threads. polling may catch a persist read edge event
         before notify_on_read is called.  */
      grpc_fd_notify_on_read(exec_ctx, se->em_fd, &se->session_read_closure);
    } else {
      gpr_log(GPR_ERROR, "Unhandled read error %s", strerror(errno));
      abort();
    }
  }
}

/* Called when the listen FD can be safely shutdown.
   Close listen FD and signal that server can be shutdown. */
static void listen_shutdown_cb(grpc_exec_ctx* exec_ctx, void* arg /*server */,
                               int success) {
  server* sv = static_cast<server*>(arg);

  grpc_fd_orphan(exec_ctx, sv->em_fd, nullptr, nullptr,
                 false /* already_closed */, "b");

  gpr_mu_lock(g_mu);
  sv->done = 1;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick", grpc_pollset_kick(exec_ctx, g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

/* Called when a new TCP connection request arrives in the listening port. */
static void listen_cb(grpc_exec_ctx* exec_ctx, void* arg, /*=sv_arg*/
                      grpc_error* error) {
  server* sv = static_cast<server*>(arg);
  int fd;
  int flags;
  session* se;
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  grpc_fd* listen_em_fd = sv->em_fd;

  if (error != GRPC_ERROR_NONE) {
    listen_shutdown_cb(exec_ctx, arg, 1);
    return;
  }

  fd = accept(grpc_fd_wrapped_fd(listen_em_fd), (struct sockaddr*)&ss, &slen);
  GPR_ASSERT(fd >= 0);
  GPR_ASSERT(fd < FD_SETSIZE);
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  se = static_cast<session*>(gpr_malloc(sizeof(*se)));
  se->sv = sv;
  se->em_fd = grpc_fd_create(fd, "listener");
  grpc_pollset_add_fd(exec_ctx, g_pollset, se->em_fd);
  GRPC_CLOSURE_INIT(&se->session_read_closure, session_read_cb, se,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_read(exec_ctx, se->em_fd, &se->session_read_closure);

  grpc_fd_notify_on_read(exec_ctx, listen_em_fd, &sv->listen_closure);
}

/* Max number of connections pending to be accepted by listen(). */
#define MAX_NUM_FD 1024

/* Start a test server, return the TCP listening port bound to listen_fd.
   listen_cb() is registered to be interested in reading from listen_fd.
   When connection request arrives, listen_cb() is called to accept the
   connection request. */
static int server_start(grpc_exec_ctx* exec_ctx, server* sv) {
  int port = 0;
  int fd;
  struct sockaddr_in sin;
  socklen_t addr_len;

  create_test_socket(port, &fd, &sin);
  addr_len = sizeof(sin);
  GPR_ASSERT(bind(fd, (struct sockaddr*)&sin, addr_len) == 0);
  GPR_ASSERT(getsockname(fd, (struct sockaddr*)&sin, &addr_len) == 0);
  port = ntohs(sin.sin_port);
  GPR_ASSERT(listen(fd, MAX_NUM_FD) == 0);

  sv->em_fd = grpc_fd_create(fd, "server");
  grpc_pollset_add_fd(exec_ctx, g_pollset, sv->em_fd);
  /* Register to be interested in reading from listen_fd. */
  GRPC_CLOSURE_INIT(&sv->listen_closure, listen_cb, sv,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_read(exec_ctx, sv->em_fd, &sv->listen_closure);

  return port;
}

/* Wait and shutdown a sever. */
static void server_wait_and_shutdown(server* sv) {
  gpr_mu_lock(g_mu);
  while (!sv->done) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                                          GRPC_MILLIS_INF_FUTURE)));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);
}

/* ===An upload client to test notify_on_write=== */

/* Client write buffer size */
#define CLIENT_WRITE_BUF_SIZE 10
/* Total number of times that the client fills up the write buffer */
#define CLIENT_TOTAL_WRITE_CNT 3

/* An upload client. */
typedef struct {
  grpc_fd* em_fd;
  char write_buf[CLIENT_WRITE_BUF_SIZE];
  ssize_t write_bytes_total;
  /* Number of times that the client fills up the write buffer and calls
     notify_on_write to schedule another write. */
  int client_write_cnt;

  int done; /* set to 1 when a client finishes sending */
  grpc_closure write_closure;
} client;

static void client_init(client* cl) {
  memset(cl->write_buf, 0, sizeof(cl->write_buf));
  cl->write_bytes_total = 0;
  cl->client_write_cnt = 0;
  cl->done = 0;
}

/* Called when a client upload session is ready to shutdown. */
static void client_session_shutdown_cb(grpc_exec_ctx* exec_ctx,
                                       void* arg /*client */, int success) {
  client* cl = static_cast<client*>(arg);
  grpc_fd_orphan(exec_ctx, cl->em_fd, nullptr, nullptr,
                 false /* already_closed */, "c");
  cl->done = 1;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick", grpc_pollset_kick(exec_ctx, g_pollset, nullptr)));
}

/* Write as much as possible, then register notify_on_write. */
static void client_session_write(grpc_exec_ctx* exec_ctx, void* arg, /*client */
                                 grpc_error* error) {
  client* cl = static_cast<client*>(arg);
  int fd = grpc_fd_wrapped_fd(cl->em_fd);
  ssize_t write_once = 0;

  if (error != GRPC_ERROR_NONE) {
    gpr_mu_lock(g_mu);
    client_session_shutdown_cb(exec_ctx, arg, 1);
    gpr_mu_unlock(g_mu);
    return;
  }

  do {
    write_once = write(fd, cl->write_buf, CLIENT_WRITE_BUF_SIZE);
    if (write_once > 0) cl->write_bytes_total += write_once;
  } while (write_once > 0);

  if (errno == EAGAIN) {
    gpr_mu_lock(g_mu);
    if (cl->client_write_cnt < CLIENT_TOTAL_WRITE_CNT) {
      GRPC_CLOSURE_INIT(&cl->write_closure, client_session_write, cl,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_notify_on_write(exec_ctx, cl->em_fd, &cl->write_closure);
      cl->client_write_cnt++;
    } else {
      client_session_shutdown_cb(exec_ctx, arg, 1);
    }
    gpr_mu_unlock(g_mu);
  } else {
    gpr_log(GPR_ERROR, "unknown errno %s", strerror(errno));
    abort();
  }
}

/* Start a client to send a stream of bytes. */
static void client_start(grpc_exec_ctx* exec_ctx, client* cl, int port) {
  int fd;
  struct sockaddr_in sin;
  create_test_socket(port, &fd, &sin);
  if (connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
    if (errno == EINPROGRESS) {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (poll(&pfd, 1, -1) == -1) {
        gpr_log(GPR_ERROR, "poll() failed during connect; errno=%d", errno);
        abort();
      }
    } else {
      gpr_log(GPR_ERROR, "Failed to connect to the server (errno=%d)", errno);
      abort();
    }
  }

  cl->em_fd = grpc_fd_create(fd, "client");
  grpc_pollset_add_fd(exec_ctx, g_pollset, cl->em_fd);

  client_session_write(exec_ctx, cl, GRPC_ERROR_NONE);
}

/* Wait for the signal to shutdown a client. */
static void client_wait_and_shutdown(client* cl) {
  gpr_mu_lock(g_mu);
  while (!cl->done) {
    grpc_pollset_worker* worker = nullptr;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                                          GRPC_MILLIS_INF_FUTURE)));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);
}

/* Test grpc_fd. Start an upload server and client, upload a stream of
   bytes from the client to the server, and verify that the total number of
   sent bytes is equal to the total number of received bytes. */
static void test_grpc_fd(void) {
  server sv;
  client cl;
  int port;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  server_init(&sv);
  port = server_start(&exec_ctx, &sv);
  client_init(&cl);
  client_start(&exec_ctx, &cl, port);
  grpc_exec_ctx_finish(&exec_ctx);
  client_wait_and_shutdown(&cl);
  server_wait_and_shutdown(&sv);
  GPR_ASSERT(sv.read_bytes_total == cl.write_bytes_total);
  gpr_log(GPR_INFO, "Total read bytes %" PRIdPTR, sv.read_bytes_total);
}

typedef struct fd_change_data {
  grpc_iomgr_cb_func cb_that_ran;
} fd_change_data;

void init_change_data(fd_change_data* fdc) { fdc->cb_that_ran = nullptr; }

void destroy_change_data(fd_change_data* fdc) {}

static void first_read_callback(grpc_exec_ctx* exec_ctx,
                                void* arg /* fd_change_data */,
                                grpc_error* error) {
  fd_change_data* fdc = static_cast<fd_change_data*>(arg);

  gpr_mu_lock(g_mu);
  fdc->cb_that_ran = first_read_callback;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick", grpc_pollset_kick(exec_ctx, g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

static void second_read_callback(grpc_exec_ctx* exec_ctx,
                                 void* arg /* fd_change_data */,
                                 grpc_error* error) {
  fd_change_data* fdc = static_cast<fd_change_data*>(arg);

  gpr_mu_lock(g_mu);
  fdc->cb_that_ran = second_read_callback;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick", grpc_pollset_kick(exec_ctx, g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

/* Test that changing the callback we use for notify_on_read actually works.
   Note that we have two different but almost identical callbacks above -- the
   point is to have two different function pointers and two different data
   pointers and make sure that changing both really works. */
static void test_grpc_fd_change(void) {
  grpc_fd* em_fd;
  fd_change_data a, b;
  int flags;
  int sv[2];
  char data;
  ssize_t result;
  grpc_closure first_closure;
  grpc_closure second_closure;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_CLOSURE_INIT(&first_closure, first_read_callback, &a,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&second_closure, second_read_callback, &b,
                    grpc_schedule_on_exec_ctx);

  init_change_data(&a);
  init_change_data(&b);

  GPR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);

  em_fd = grpc_fd_create(sv[0], "test_grpc_fd_change");
  grpc_pollset_add_fd(&exec_ctx, g_pollset, em_fd);

  /* Register the first callback, then make its FD readable */
  grpc_fd_notify_on_read(&exec_ctx, em_fd, &first_closure);
  data = 0;
  result = write(sv[1], &data, 1);
  GPR_ASSERT(result == 1);

  /* And now wait for it to run. */
  gpr_mu_lock(g_mu);
  while (a.cb_that_ran == nullptr) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                                          GRPC_MILLIS_INF_FUTURE)));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(a.cb_that_ran == first_read_callback);
  gpr_mu_unlock(g_mu);

  /* And drain the socket so we can generate a new read edge */
  result = read(sv[0], &data, 1);
  GPR_ASSERT(result == 1);

  /* Now register a second callback with distinct change data, and do the same
     thing again. */
  grpc_fd_notify_on_read(&exec_ctx, em_fd, &second_closure);
  data = 0;
  result = write(sv[1], &data, 1);
  GPR_ASSERT(result == 1);

  gpr_mu_lock(g_mu);
  while (b.cb_that_ran == nullptr) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                                          GRPC_MILLIS_INF_FUTURE)));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  /* Except now we verify that second_read_callback ran instead */
  GPR_ASSERT(b.cb_that_ran == second_read_callback);
  gpr_mu_unlock(g_mu);

  grpc_fd_orphan(&exec_ctx, em_fd, nullptr, nullptr, false /* already_closed */,
                 "d");
  grpc_exec_ctx_finish(&exec_ctx);
  destroy_change_data(&a);
  destroy_change_data(&b);
  close(sv[1]);
}

static void destroy_pollset(grpc_exec_ctx* exec_ctx, void* p,
                            grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(g_pollset, &g_mu);
  test_grpc_fd();
  test_grpc_fd_change();
  GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, g_pollset, &destroyed);
  grpc_exec_ctx_flush(&exec_ctx);
  gpr_free(g_pollset);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();
  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
