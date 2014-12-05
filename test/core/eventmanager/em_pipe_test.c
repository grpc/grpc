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

/* Test grpc_em_fd with pipe. The test creates a pipe with non-blocking mode,
   sends a stream of bytes through the pipe, and verifies that all bytes are
   received. */
#include "src/core/eventmanager/em.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

/* Operation for fcntl() to set pipe buffer size. */
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ (1024 + 7)
#endif

#define TOTAL_WRITE 3 /* total number of times that the write buffer is full. \
                         */
#define BUF_SIZE 1024
char read_buf[BUF_SIZE];
char write_buf[BUF_SIZE];

typedef struct {
  int fd[2];
  grpc_em em;
  grpc_em_fd read_em_fd;
  grpc_em_fd write_em_fd;
  int num_write; /* number of times that the write buffer is full*/
  ssize_t bytes_written_total; /* total number of bytes written to the pipe */
  ssize_t bytes_read_total;    /* total number of bytes read from the pipe */
  pthread_mutex_t mu;          /* protect cv and done */
  pthread_cond_t cv;           /* signaled when read finished */
  int done;                    /* set to 1 when read finished */
} async_pipe;

void write_shutdown_cb(void *arg, /*async_pipe*/
                       enum grpc_em_cb_status status) {
  async_pipe *ap = arg;
  grpc_em_fd_destroy(&ap->write_em_fd);
}

void write_cb(void *arg, /*async_pipe*/ enum grpc_em_cb_status status) {
  async_pipe *ap = arg;
  ssize_t bytes_written = 0;

  if (status == GRPC_CALLBACK_CANCELLED) {
    write_shutdown_cb(arg, GRPC_CALLBACK_SUCCESS);
    return;
  }

  do {
    bytes_written = write(ap->fd[1], write_buf, BUF_SIZE);
    if (bytes_written > 0) ap->bytes_written_total += bytes_written;
  } while (bytes_written > 0);

  if (errno == EAGAIN) {
    if (ap->num_write < TOTAL_WRITE) {
      ap->num_write++;
      grpc_em_fd_notify_on_write(&ap->write_em_fd, write_cb, ap,
                                 gpr_inf_future);
    } else {
      /* Note that this could just shut down directly; doing a trip through the
         shutdown path serves only a demonstration of the API. */
      grpc_em_fd_shutdown(&ap->write_em_fd);
      grpc_em_fd_notify_on_write(&ap->write_em_fd, write_cb, ap,
                                 gpr_inf_future);
    }
  } else {
    GPR_ASSERT(0 && strcat("unknown errno: ", strerror(errno)));
  }
}

void read_shutdown_cb(void *arg, /*async_pipe*/ enum grpc_em_cb_status status) {
  async_pipe *ap = arg;
  grpc_em_fd_destroy(&ap->read_em_fd);
  pthread_mutex_lock(&ap->mu);
  if (ap->done == 0) {
    ap->done = 1;
    pthread_cond_signal(&ap->cv);
  }
  pthread_mutex_unlock(&ap->mu);
}

void read_cb(void *arg, /*async_pipe*/ enum grpc_em_cb_status status) {
  async_pipe *ap = arg;
  ssize_t bytes_read = 0;

  if (status == GRPC_CALLBACK_CANCELLED) {
    read_shutdown_cb(arg, GRPC_CALLBACK_SUCCESS);
    return;
  }

  do {
    bytes_read = read(ap->fd[0], read_buf, BUF_SIZE);
    if (bytes_read > 0) ap->bytes_read_total += bytes_read;
  } while (bytes_read > 0);

  if (bytes_read == 0) {
    /* Note that this could just shut down directly; doing a trip through the
       shutdown path serves only a demonstration of the API. */
    grpc_em_fd_shutdown(&ap->read_em_fd);
    grpc_em_fd_notify_on_read(&ap->read_em_fd, read_cb, ap, gpr_inf_future);
  } else if (bytes_read == -1) {
    if (errno == EAGAIN) {
      grpc_em_fd_notify_on_read(&ap->read_em_fd, read_cb, ap, gpr_inf_future);
    } else {
      GPR_ASSERT(0 && strcat("unknown errno: ", strerror(errno)));
    }
  }
}

void dummy_cb(void *arg, /*async_pipe*/ enum grpc_em_cb_status status) {}

void async_pipe_init(async_pipe *ap) {
  int i;

  ap->num_write = 0;
  ap->bytes_written_total = 0;
  ap->bytes_read_total = 0;

  pthread_mutex_init(&ap->mu, NULL);
  pthread_cond_init(&ap->cv, NULL);
  ap->done = 0;

  GPR_ASSERT(0 == pipe(ap->fd));
  for (i = 0; i < 2; i++) {
    int flags = fcntl(ap->fd[i], F_GETFL, 0);
    GPR_ASSERT(fcntl(ap->fd[i], F_SETFL, flags | O_NONBLOCK) == 0);
    GPR_ASSERT(fcntl(ap->fd[i], F_SETPIPE_SZ, 4096) == 4096);
  }

  grpc_em_init(&ap->em);
  grpc_em_fd_init(&ap->read_em_fd, &ap->em, ap->fd[0]);
  grpc_em_fd_init(&ap->write_em_fd, &ap->em, ap->fd[1]);
}

static void async_pipe_start(async_pipe *ap) {
  grpc_em_fd_notify_on_read(&ap->read_em_fd, read_cb, ap, gpr_inf_future);
  grpc_em_fd_notify_on_write(&ap->write_em_fd, write_cb, ap, gpr_inf_future);
}

static void async_pipe_wait_destroy(async_pipe *ap) {
  pthread_mutex_lock(&ap->mu);
  while (!ap->done) pthread_cond_wait(&ap->cv, &ap->mu);
  pthread_mutex_unlock(&ap->mu);
  pthread_mutex_destroy(&ap->mu);
  pthread_cond_destroy(&ap->cv);

  grpc_em_destroy(&ap->em);
}

int main(int argc, char **argv) {
  async_pipe ap;
  grpc_test_init(argc, argv);
  async_pipe_init(&ap);
  async_pipe_start(&ap);
  async_pipe_wait_destroy(&ap);
  GPR_ASSERT(ap.bytes_read_total == ap.bytes_written_total);
  gpr_log(GPR_INFO, "read total bytes %d", ap.bytes_read_total);
  return 0;
}
