/*
 *
 * Copyright 2014, Google Inc.
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

/* Test gRPC event manager with a simple TCP upload server and client. */
#include "src/core/eventmanager/em.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

/* buffer size used to send and receive data.
   1024 is the minimal value to set TCP send and receive buffer. */
#define BUF_SIZE 1024

/* Create a test socket with the right properties for testing.
   port is the TCP port to listen or connect to.
   Return a socket FD and sockaddr_in. */
static void create_test_socket(int port, int *socket_fd,
                               struct sockaddr_in *sin) {
  int fd;
  int one = 1;
  int buf_size = BUF_SIZE;
  int flags;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  /* Reset the size of socket send buffer to the minimal value to facilitate
     buffer filling up and triggering notify_on_write  */
  GPR_ASSERT(
      setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) != -1);
  GPR_ASSERT(
      setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) != -1);
  /* Make fd non-blocking */
  flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  *socket_fd = fd;

  /* Use local address for test */
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = 0;
  sin->sin_port = htons(port);
}

/* Dummy gRPC callback */
void no_op_cb(void *arg, enum grpc_em_cb_status status) {}

/* =======An upload server to test notify_on_read===========
   The server simply reads and counts a stream of bytes. */

/* An upload server. */
typedef struct {
  grpc_em em;               /* event manger used by the sever */
  grpc_em_fd em_fd;         /* listening fd */
  ssize_t read_bytes_total; /* total number of received bytes */
  gpr_mu mu;                /* protect done and done_cv */
  gpr_cv done_cv;           /* signaled when a server finishes serving */
  int done;                 /* set to 1 when a server finishes serving */
} server;

static void server_init(server *sv) {
  GPR_ASSERT(grpc_em_init(&sv->em) == GRPC_EM_OK);
  sv->read_bytes_total = 0;
  gpr_mu_init(&sv->mu);
  gpr_cv_init(&sv->done_cv);
  sv->done = 0;
}

/* An upload session.
   Created when a new upload request arrives in the server. */
typedef struct {
  server *sv;              /* not owned by a single session */
  grpc_em_fd em_fd;        /* fd to read upload bytes */
  char read_buf[BUF_SIZE]; /* buffer to store upload bytes */
} session;

/* Called when an upload session can be safely shutdown.
   Close session FD and start to shutdown listen FD. */
static void session_shutdown_cb(void *arg, /*session*/
                                enum grpc_em_cb_status status) {
  session *se = arg;
  server *sv = se->sv;
  grpc_em_fd_destroy(&se->em_fd);
  gpr_free(se);
  /* Start to shutdown listen fd. */
  grpc_em_fd_shutdown(&sv->em_fd);
}

/* Called when data become readable in a session. */
static void session_read_cb(void *arg, /*session*/
                            enum grpc_em_cb_status status) {
  session *se = arg;
  int fd = grpc_em_fd_get(&se->em_fd);

  ssize_t read_once = 0;
  ssize_t read_total = 0;

  if (status == GRPC_CALLBACK_CANCELLED) {
    session_shutdown_cb(arg, GRPC_CALLBACK_SUCCESS);
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
    grpc_em_fd_shutdown(&se->em_fd);
    grpc_em_fd_notify_on_read(&se->em_fd, session_read_cb, se, gpr_inf_future);
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
      GPR_ASSERT(grpc_em_fd_notify_on_read(&se->em_fd, session_read_cb, se,
                                           gpr_inf_future) == GRPC_EM_OK);
    } else {
      gpr_log(GPR_ERROR, "Unhandled read error %s", strerror(errno));
      GPR_ASSERT(0);
    }
  }
}

/* Called when the listen FD can be safely shutdown.
   Close listen FD and signal that server can be shutdown. */
static void listen_shutdown_cb(void *arg /*server*/,
                               enum grpc_em_cb_status status) {
  server *sv = arg;

  grpc_em_fd_destroy(&sv->em_fd);

  gpr_mu_lock(&sv->mu);
  sv->done = 1;
  gpr_cv_signal(&sv->done_cv);
  gpr_mu_unlock(&sv->mu);
}

/* Called when a new TCP connection request arrives in the listening port. */
static void listen_cb(void *arg, /*=sv_arg*/
                      enum grpc_em_cb_status status) {
  server *sv = arg;
  int fd;
  int flags;
  session *se;
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  struct grpc_em_fd *listen_em_fd = &sv->em_fd;

  if (status == GRPC_CALLBACK_CANCELLED) {
    listen_shutdown_cb(arg, GRPC_CALLBACK_SUCCESS);
    return;
  }

  fd = accept(grpc_em_fd_get(listen_em_fd), (struct sockaddr *)&ss, &slen);
  GPR_ASSERT(fd >= 0);
  GPR_ASSERT(fd < FD_SETSIZE);
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  se = gpr_malloc(sizeof(*se));
  se->sv = sv;
  GPR_ASSERT(grpc_em_fd_init(&se->em_fd, &sv->em, fd) == GRPC_EM_OK);
  GPR_ASSERT(grpc_em_fd_notify_on_read(&se->em_fd, session_read_cb, se,
                                       gpr_inf_future) == GRPC_EM_OK);

  GPR_ASSERT(grpc_em_fd_notify_on_read(listen_em_fd, listen_cb, sv,
                                       gpr_inf_future) == GRPC_EM_OK);
}

/* Max number of connections pending to be accepted by listen(). */
#define MAX_NUM_FD 1024

/* Start a test server, return the TCP listening port bound to listen_fd.
   listen_cb() is registered to be interested in reading from listen_fd.
   When connection request arrives, listen_cb() is called to accept the
   connection request. */
static int server_start(server *sv) {
  int port = 0;
  int fd;
  struct sockaddr_in sin;
  socklen_t addr_len;

  create_test_socket(port, &fd, &sin);
  addr_len = sizeof(sin);
  GPR_ASSERT(bind(fd, (struct sockaddr *)&sin, addr_len) == 0);
  GPR_ASSERT(getsockname(fd, (struct sockaddr *)&sin, &addr_len) == GRPC_EM_OK);
  port = ntohs(sin.sin_port);
  GPR_ASSERT(listen(fd, MAX_NUM_FD) == 0);

  GPR_ASSERT(grpc_em_fd_init(&sv->em_fd, &sv->em, fd) == GRPC_EM_OK);
  /* Register to be interested in reading from listen_fd. */
  GPR_ASSERT(grpc_em_fd_notify_on_read(&sv->em_fd, listen_cb, sv,
                                       gpr_inf_future) == GRPC_EM_OK);

  return port;
}

/* Wait and shutdown a sever. */
static void server_wait_and_shutdown(server *sv) {
  gpr_mu_lock(&sv->mu);
  while (!sv->done) gpr_cv_wait(&sv->done_cv, &sv->mu, gpr_inf_future);
  gpr_mu_unlock(&sv->mu);

  gpr_mu_destroy(&sv->mu);
  gpr_cv_destroy(&sv->done_cv);

  GPR_ASSERT(grpc_em_destroy(&sv->em) == GRPC_EM_OK);
}

/* ===An upload client to test notify_on_write=== */

/* Client write buffer size */
#define CLIENT_WRITE_BUF_SIZE 10
/* Total number of times that the client fills up the write buffer */
#define CLIENT_TOTAL_WRITE_CNT 3

/* An upload client. */
typedef struct {
  grpc_em em;
  grpc_em_fd em_fd;
  char write_buf[CLIENT_WRITE_BUF_SIZE];
  ssize_t write_bytes_total;
  /* Number of times that the client fills up the write buffer and calls
     notify_on_write to schedule another write. */
  int client_write_cnt;

  gpr_mu mu;      /* protect done and done_cv */
  gpr_cv done_cv; /* signaled when a client finishes sending */
  int done;       /* set to 1 when a client finishes sending */
} client;

static void client_init(client *cl) {
  GPR_ASSERT(grpc_em_init(&cl->em) == GRPC_EM_OK);
  memset(cl->write_buf, 0, sizeof(cl->write_buf));
  cl->write_bytes_total = 0;
  cl->client_write_cnt = 0;
  gpr_mu_init(&cl->mu);
  gpr_cv_init(&cl->done_cv);
  cl->done = 0;
}

/* Called when a client upload session is ready to shutdown. */
static void client_session_shutdown_cb(void *arg /*client*/,
                                       enum grpc_em_cb_status status) {
  client *cl = arg;
  grpc_em_fd_destroy(&cl->em_fd);
  gpr_mu_lock(&cl->mu);
  cl->done = 1;
  gpr_cv_signal(&cl->done_cv);
  gpr_mu_unlock(&cl->mu);
}

/* Write as much as possible, then register notify_on_write. */
static void client_session_write(void *arg, /*client*/
                                 enum grpc_em_cb_status status) {
  client *cl = arg;
  int fd = grpc_em_fd_get(&cl->em_fd);
  ssize_t write_once = 0;

  if (status == GRPC_CALLBACK_CANCELLED) {
    client_session_shutdown_cb(arg, GRPC_CALLBACK_SUCCESS);
    return;
  }

  do {
    write_once = write(fd, cl->write_buf, CLIENT_WRITE_BUF_SIZE);
    if (write_once > 0) cl->write_bytes_total += write_once;
  } while (write_once > 0);

  if (errno == EAGAIN) {
    gpr_mu_lock(&cl->mu);
    if (cl->client_write_cnt < CLIENT_TOTAL_WRITE_CNT) {
      GPR_ASSERT(grpc_em_fd_notify_on_write(&cl->em_fd, client_session_write,
                                            cl, gpr_inf_future) == GRPC_EM_OK);
      cl->client_write_cnt++;
    } else {
      close(fd);
      grpc_em_fd_shutdown(&cl->em_fd);
      grpc_em_fd_notify_on_write(&cl->em_fd, client_session_write, cl,
                                 gpr_inf_future);
    }
    gpr_mu_unlock(&cl->mu);
  } else {
    gpr_log(GPR_ERROR, "unknown errno %s", strerror(errno));
    GPR_ASSERT(0);
  }
}

/* Start a client to send a stream of bytes. */
static void client_start(client *cl, int port) {
  int fd;
  struct sockaddr_in sin;
  create_test_socket(port, &fd, &sin);
  if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1 &&
      errno != EINPROGRESS) {
    gpr_log(GPR_ERROR, "Failed to connect to the server");
    GPR_ASSERT(0);
  }

  GPR_ASSERT(grpc_em_fd_init(&cl->em_fd, &cl->em, fd) == GRPC_EM_OK);

  client_session_write(cl, GRPC_CALLBACK_SUCCESS);
}

/* Wait for the signal to shutdown a client. */
static void client_wait_and_shutdown(client *cl) {
  gpr_mu_lock(&cl->mu);
  while (!cl->done) gpr_cv_wait(&cl->done_cv, &cl->mu, gpr_inf_future);
  gpr_mu_unlock(&cl->mu);

  gpr_mu_destroy(&cl->mu);
  gpr_cv_destroy(&cl->done_cv);

  GPR_ASSERT(grpc_em_destroy(&cl->em) == GRPC_EM_OK);
}

/* Test grpc_em_fd. Start an upload server and client, upload a stream of
   bytes from the client to the server, and verify that the total number of
   sent bytes is equal to the total number of received bytes. */
static void test_grpc_em_fd() {
  server sv;
  client cl;
  int port;

  server_init(&sv);
  port = server_start(&sv);
  client_init(&cl);
  client_start(&cl, port);
  client_wait_and_shutdown(&cl);
  server_wait_and_shutdown(&sv);
  GPR_ASSERT(sv.read_bytes_total == cl.write_bytes_total);
  gpr_log(GPR_INFO, "Total read bytes %d", sv.read_bytes_total);
}

typedef struct fd_change_data {
  gpr_mu mu;
  gpr_cv cv;
  void (*cb_that_ran)(void *, enum grpc_em_cb_status);
} fd_change_data;

void init_change_data(fd_change_data *fdc) {
  gpr_mu_init(&fdc->mu);
  gpr_cv_init(&fdc->cv);
  fdc->cb_that_ran = NULL;
}

void destroy_change_data(fd_change_data *fdc) {
  gpr_mu_destroy(&fdc->mu);
  gpr_cv_destroy(&fdc->cv);
}

static void first_read_callback(void *arg /* fd_change_data */,
                                enum grpc_em_cb_status status) {
  fd_change_data *fdc = arg;

  gpr_mu_lock(&fdc->mu);
  fdc->cb_that_ran = first_read_callback;
  gpr_cv_signal(&fdc->cv);
  gpr_mu_unlock(&fdc->mu);
}

static void second_read_callback(void *arg /* fd_change_data */,
                                 enum grpc_em_cb_status status) {
  fd_change_data *fdc = arg;

  gpr_mu_lock(&fdc->mu);
  fdc->cb_that_ran = second_read_callback;
  gpr_cv_signal(&fdc->cv);
  gpr_mu_unlock(&fdc->mu);
}

/* Test that changing the callback we use for notify_on_read actually works.
   Note that we have two different but almost identical callbacks above -- the
   point is to have two different function pointers and two different data
   pointers and make sure that changing both really works. */
static void test_grpc_em_fd_change() {
  grpc_em em;
  grpc_em_fd em_fd;
  fd_change_data a, b;
  int flags;
  int sv[2];
  char data;
  int result;

  init_change_data(&a);
  init_change_data(&b);

  GPR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);

  grpc_em_init(&em);
  grpc_em_fd_init(&em_fd, &em, sv[0]);

  /* Register the first callback, then make its FD readable */
  grpc_em_fd_notify_on_read(&em_fd, first_read_callback, &a, gpr_inf_future);
  data = 0;
  result = write(sv[1], &data, 1);
  GPR_ASSERT(result == 1);

  /* And now wait for it to run. */
  gpr_mu_lock(&a.mu);
  while (a.cb_that_ran == NULL) {
    gpr_cv_wait(&a.cv, &a.mu, gpr_inf_future);
  }
  GPR_ASSERT(a.cb_that_ran == first_read_callback);
  gpr_mu_unlock(&a.mu);

  /* And drain the socket so we can generate a new read edge */
  result = read(sv[0], &data, 1);
  GPR_ASSERT(result == 1);

  /* Now register a second callback with distinct change data, and do the same
     thing again. */
  grpc_em_fd_notify_on_read(&em_fd, second_read_callback, &b, gpr_inf_future);
  data = 0;
  result = write(sv[1], &data, 1);
  GPR_ASSERT(result == 1);

  gpr_mu_lock(&b.mu);
  while (b.cb_that_ran == NULL) {
    gpr_cv_wait(&b.cv, &b.mu, gpr_inf_future);
  }
  /* Except now we verify that second_read_callback ran instead */
  GPR_ASSERT(b.cb_that_ran == second_read_callback);
  gpr_mu_unlock(&b.mu);

  grpc_em_fd_destroy(&em_fd);
  grpc_em_destroy(&em);
  destroy_change_data(&a);
  destroy_change_data(&b);
  close(sv[0]);
  close(sv[1]);
}

void timeout_callback(void *arg, enum grpc_em_cb_status status) {
  if (status == GRPC_CALLBACK_TIMED_OUT) {
    gpr_event_set(arg, (void *)1);
  } else {
    gpr_event_set(arg, (void *)2);
  }
}

void test_grpc_em_fd_notify_timeout() {
  grpc_em em;
  grpc_em_fd em_fd;
  gpr_event ev;
  int flags;
  int sv[2];
  gpr_timespec timeout;
  gpr_timespec deadline;

  gpr_event_init(&ev);

  GPR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);

  grpc_em_init(&em);
  grpc_em_fd_init(&em_fd, &em, sv[0]);

  timeout = gpr_time_from_micros(1000000);
  deadline = gpr_time_add(gpr_now(), timeout);

  grpc_em_fd_notify_on_read(&em_fd, timeout_callback, &ev, deadline);

  GPR_ASSERT(gpr_event_wait(&ev, gpr_time_add(deadline, timeout)));

  GPR_ASSERT(gpr_event_get(&ev) == (void *)1);
  grpc_em_fd_destroy(&em_fd);
  grpc_em_destroy(&em);
  close(sv[1]);
}

typedef struct {
  grpc_em *em;
  gpr_cv cv;
  gpr_mu mu;
  int counter;
  int done_success_ctr;
  int done_cancel_ctr;
  int done;
  gpr_event fcb_arg;
  grpc_em_cb_status status;
} alarm_arg;

static void followup_cb(void *arg, grpc_em_cb_status status) {
  gpr_event_set((gpr_event *)arg, arg);
}

/* Called when an alarm expires. */
static void alarm_cb(void *arg /* alarm_arg */, grpc_em_cb_status status) {
  alarm_arg *a = arg;
  gpr_mu_lock(&a->mu);
  if (status == GRPC_CALLBACK_SUCCESS) {
    a->counter++;
    a->done_success_ctr++;
  } else if (status == GRPC_CALLBACK_CANCELLED) {
    a->done_cancel_ctr++;
  } else {
    GPR_ASSERT(0);
  }
  a->done = 1;
  a->status = status;
  gpr_cv_signal(&a->cv);
  gpr_mu_unlock(&a->mu);
  grpc_em_add_callback(a->em, followup_cb, &a->fcb_arg);
}

/* Test grpc_em_alarm add and cancel. */
static void test_grpc_em_alarm() {
  struct grpc_em em;
  struct grpc_em_alarm alarm;
  struct grpc_em_alarm alarm_to_cancel;
  gpr_timespec tv0 = {0, 1};
  /* Timeout on the alarm cond. var, so make big enough to absorb time
     deviations. Otherwise, operations after wait will not be properly ordered
   */
  gpr_timespec tv1 = gpr_time_from_micros(200000);
  gpr_timespec tv2 = {0, 1};
  gpr_timespec alarm_deadline;
  gpr_timespec followup_deadline;

  alarm_arg arg;
  alarm_arg arg2;
  void *fdone;

  GPR_ASSERT(grpc_em_init(&em) == GRPC_EM_OK);

  arg.em = &em;
  arg.counter = 0;
  arg.status = GRPC_CALLBACK_DO_NOT_USE;
  arg.done_success_ctr = 0;
  arg.done_cancel_ctr = 0;
  arg.done = 0;
  gpr_mu_init(&arg.mu);
  gpr_cv_init(&arg.cv);
  gpr_event_init(&arg.fcb_arg);

  GPR_ASSERT(grpc_em_alarm_init(&alarm, &em, alarm_cb, &arg) == GRPC_EM_OK);
  GPR_ASSERT(grpc_em_alarm_add(&alarm, gpr_time_add(tv0, gpr_now())) ==
             GRPC_EM_OK);

  alarm_deadline = gpr_time_add(gpr_now(), tv1);
  gpr_mu_lock(&arg.mu);
  while (arg.done == 0) {
    gpr_cv_wait(&arg.cv, &arg.mu, alarm_deadline);
  }
  gpr_mu_unlock(&arg.mu);

  followup_deadline = gpr_time_add(gpr_now(), tv1);
  fdone = gpr_event_wait(&arg.fcb_arg, followup_deadline);

  if (arg.counter != 1) {
    gpr_log(GPR_ERROR, "Alarm callback not called");
    GPR_ASSERT(0);
  } else if (arg.done_success_ctr != 1) {
    gpr_log(GPR_ERROR, "Alarm done callback not called with success");
    GPR_ASSERT(0);
  } else if (arg.done_cancel_ctr != 0) {
    gpr_log(GPR_ERROR, "Alarm done callback called with cancel");
    GPR_ASSERT(0);
  } else if (arg.status == GRPC_CALLBACK_DO_NOT_USE) {
    gpr_log(GPR_ERROR, "Alarm callback without status");
    GPR_ASSERT(0);
  } else {
    gpr_log(GPR_INFO, "Alarm callback called successfully");
  }

  if (fdone != (void *)&arg.fcb_arg) {
    gpr_log(GPR_ERROR, "Followup callback #1 not invoked properly %p %p", fdone,
            &arg.fcb_arg);
    GPR_ASSERT(0);
  }
  gpr_cv_destroy(&arg.cv);
  gpr_mu_destroy(&arg.mu);

  arg2.em = &em;
  arg2.counter = 0;
  arg2.status = GRPC_CALLBACK_DO_NOT_USE;
  arg2.done_success_ctr = 0;
  arg2.done_cancel_ctr = 0;
  arg2.done = 0;
  gpr_mu_init(&arg2.mu);
  gpr_cv_init(&arg2.cv);
  gpr_event_init(&arg2.fcb_arg);

  GPR_ASSERT(grpc_em_alarm_init(&alarm_to_cancel, &em, alarm_cb, &arg2) ==
             GRPC_EM_OK);
  GPR_ASSERT(grpc_em_alarm_add(&alarm_to_cancel,
                               gpr_time_add(tv2, gpr_now())) == GRPC_EM_OK);
  switch (grpc_em_alarm_cancel(&alarm_to_cancel)) {
    case GRPC_EM_OK:
      gpr_log(GPR_INFO, "Alarm cancel succeeded");
      break;
    case GRPC_EM_ERROR:
      gpr_log(GPR_ERROR, "Alarm cancel failed");
      GPR_ASSERT(0);
      break;
    case GRPC_EM_INVALID_ARGUMENTS:
      gpr_log(GPR_ERROR, "Alarm cancel failed with bad response code");
      gpr_log(GPR_ERROR, "Current value of triggered is %d\n",
              (int)alarm_to_cancel.triggered);
      GPR_ASSERT(0);
      break;
  }

  alarm_deadline = gpr_time_add(gpr_now(), tv1);
  gpr_mu_lock(&arg2.mu);
  while (arg2.done == 0) {
    gpr_cv_wait(&arg2.cv, &arg2.mu, alarm_deadline);
  }
  gpr_mu_unlock(&arg2.mu);

  followup_deadline = gpr_time_add(gpr_now(), tv1);
  fdone = gpr_event_wait(&arg2.fcb_arg, followup_deadline);

  if (arg2.counter != arg2.done_success_ctr) {
    gpr_log(GPR_ERROR, "Alarm callback called but didn't lead to done success");
    GPR_ASSERT(0);
  } else if (arg2.done_success_ctr && arg2.done_cancel_ctr) {
    gpr_log(GPR_ERROR, "Alarm done callback called with success and cancel");
    GPR_ASSERT(0);
  } else if (arg2.done_cancel_ctr + arg2.done_success_ctr != 1) {
    gpr_log(GPR_ERROR, "Alarm done callback called incorrect number of times");
    GPR_ASSERT(0);
  } else if (arg2.status == GRPC_CALLBACK_DO_NOT_USE) {
    gpr_log(GPR_ERROR, "Alarm callback without status");
    GPR_ASSERT(0);
  } else if (arg2.done_success_ctr) {
    gpr_log(GPR_INFO, "Alarm callback executed before cancel");
    gpr_log(GPR_INFO, "Current value of triggered is %d\n",
            (int)alarm_to_cancel.triggered);
  } else if (arg2.done_cancel_ctr) {
    gpr_log(GPR_INFO, "Alarm callback canceled");
    gpr_log(GPR_INFO, "Current value of triggered is %d\n",
            (int)alarm_to_cancel.triggered);
  } else {
    gpr_log(GPR_ERROR, "Alarm cancel test should not be here");
    GPR_ASSERT(0);
  }

  if (fdone != (void *)&arg2.fcb_arg) {
    gpr_log(GPR_ERROR, "Followup callback #2 not invoked properly %p %p", fdone,
            &arg2.fcb_arg);
    GPR_ASSERT(0);
  }
  gpr_cv_destroy(&arg2.cv);
  gpr_mu_destroy(&arg2.mu);

  GPR_ASSERT(grpc_em_destroy(&em) == GRPC_EM_OK);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_grpc_em_alarm();
  test_grpc_em_fd();
  test_grpc_em_fd_change();
  test_grpc_em_fd_notify_timeout();
  return 0;
}
